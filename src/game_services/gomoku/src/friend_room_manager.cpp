/**
 * @file friend_room_manager.cpp
 * @brief 好友房间管理器实现
 * @author AI Assistant
 * @date 2026-02-23
 */

#include "friend_room_manager.h"
#include <common/logger/logger.h>
#include <algorithm>
#include <sstream>
#include <random>

namespace game_services {
namespace gomoku {

FriendRoomManager::FriendRoomManager(
    std::shared_ptr<IFriendService> friend_service,
    const FriendRoomManagerConfig& config)
    : friend_service_(std::move(friend_service))
    , config_(config) {
}

FriendRoomManager::~FriendRoomManager() {
    shutdown();
}

bool FriendRoomManager::initialize() {
    if (!config_.enabled) {
        LOG_INFO("FriendRoomManager is disabled");
        return true;
    }

    LOG_INFO("FriendRoomManager initialized");
    return true;
}

void FriendRoomManager::shutdown() {
    std::lock_guard<std::mutex> rooms_lock(rooms_mutex_);
    std::lock_guard<std::mutex> invites_lock(invites_mutex_);

    rooms_.clear();
    invites_.clear();
    user_to_room_.clear();
    user_invites_.clear();
    room_invites_.clear();

    LOG_INFO("FriendRoomManager shutdown");
}

std::optional<std::string> FriendRoomManager::createFriendRoom(
    const std::string& host_user_id,
    const FriendRoomConfig& config) {

    if (!config_.enabled) {
        LOG_WARNING("FriendRoomManager is disabled");
        return std::nullopt;
    }

    // 检查用户是否已有太多房间
    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        int user_room_count = 0;
        for (const auto& [id, room] : rooms_) {
            if (room.config.host_user_id == host_user_id) {
                user_room_count++;
            }
        }
        if (user_room_count >= config_.max_rooms_per_user) {
            LOG_WARNING("User has reached max room limit");
            return std::nullopt;
        }
    }

    std::string room_id = generateRoomId();

    FriendRoomState state;
    state.room_id = room_id;
    state.config = config;
    state.config.room_id = room_id;
    state.config.host_user_id = host_user_id;
    state.created_at = std::chrono::system_clock::now();

    // 房主自动加入
    state.player_ids.push_back(host_user_id);
    state.player_ready_status[host_user_id] = false;

    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        rooms_[room_id] = state;
        user_to_room_[host_user_id] = room_id;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        total_rooms_created_++;
    }

    triggerEvent(FriendRoomEventType::ROOM_CREATED, room_id, host_user_id,
                  {{"config", config.toJson()}});

    LOG_INFO("Created friend room");
    return room_id;
}

bool FriendRoomManager::closeFriendRoom(const std::string& room_id, const std::string& by_user_id) {
    std::lock_guard<std::mutex> lock(rooms_mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return false;
    }

    // 只有房主可以关闭房间
    if (it->second.config.host_user_id != by_user_id) {
        LOG_WARNING("User attempted to close room they don't own");
        return false;
    }

    // 移除用户到房间的映射
    for (const auto& user_id : it->second.player_ids) {
        user_to_room_.erase(user_id);
    }
    for (const auto& user_id : it->second.spectator_ids) {
        user_to_room_.erase(user_id);
    }

    rooms_.erase(it);

    triggerEvent(FriendRoomEventType::ROOM_CLOSED, room_id, by_user_id);

    LOG_INFO("Closed friend room");
    return true;
}

std::optional<FriendRoomState> FriendRoomManager::getRoomState(const std::string& room_id) const {
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<FriendRoomState> FriendRoomManager::getUserRooms(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(rooms_mutex_);
    std::vector<FriendRoomState> result;
    for (const auto& [id, room] : rooms_) {
        if (room.config.host_user_id == user_id) {
            result.push_back(room);
        }
    }
    return result;
}

std::optional<std::string> FriendRoomManager::sendInvite(
    const std::string& room_id,
    const std::string& from_user_id,
    const std::string& to_user_id) {

    if (!config_.enabled) {
        return std::nullopt;
    }

    // 检查房间是否存在
    FriendRoomState room_state;
    {
        std::lock_guard<std::mutex> lock(rooms_mutex_);
        auto it = rooms_.find(room_id);
        if (it == rooms_.end()) {
            LOG_WARNING("Room not found for invite");
            return std::nullopt;
        }
        room_state = it->second;
    }

    // 检查发送者是否是房主
    if (room_state.config.host_user_id != from_user_id) {
        LOG_WARNING("User is not room host, cannot invite");
        return std::nullopt;
    }

    // 检查邀请数量限制
    {
        std::lock_guard<std::mutex> lock(invites_mutex_);
        auto room_invites_it = room_invites_.find(room_id);
        if (room_invites_it != room_invites_.end() &&
            room_invites_it->second.size() >= static_cast<size_t>(config_.max_invites_per_room)) {
            LOG_WARNING("Room has reached max invite limit");
            return std::nullopt;
        }
    }

    // 检查好友关系
    if (friend_service_) {
        if (config_.require_mutual_friends) {
            if (!friend_service_->areFriends(from_user_id, to_user_id)) {
                LOG_WARNING("Users are not mutual friends");
                return std::nullopt;
            }
        }
    }

    // 创建邀请
    std::string invite_id = generateInviteId();
    FriendRoomInvite invite;
    invite.invite_id = invite_id;
    invite.room_id = room_id;
    invite.host_user_id = from_user_id;
    invite.target_user_id = to_user_id;
    invite.room_name = room_state.config.room_name;
    invite.game_mode = room_state.config.game_mode;
    invite.created_at = std::chrono::system_clock::now();
    invite.expires_at = invite.created_at + std::chrono::minutes(config_.invite_expiry_minutes);

    {
        std::lock_guard<std::mutex> lock(invites_mutex_);
        invites_[invite_id] = invite;
        user_invites_[to_user_id].push_back(invite_id);
        room_invites_[room_id].push_back(invite_id);
    }

    // 通知被邀请者
    notifyUser(to_user_id, "friend_room_invite", invite.toJson());

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        total_invites_sent_++;
    }

    triggerEvent(FriendRoomEventType::INVITE_SENT, room_id, from_user_id,
                  {{"invite_id", invite_id}, {"target_user_id", to_user_id}});

    LOG_INFO("Sent invite");
    return invite_id;
}

bool FriendRoomManager::acceptInvite(const std::string& invite_id, const std::string& by_user_id) {
    FriendRoomInvite invite;
    {
        std::lock_guard<std::mutex> lock(invites_mutex_);
        auto it = invites_.find(invite_id);
        if (it == invites_.end()) {
            LOG_WARNING("Invite not found");
            return false;
        }

        invite = it->second;

        // 验证邀请
        if (invite.target_user_id != by_user_id) {
            LOG_WARNING("User is not the target of invite");
            return false;
        }

        if (!isInviteValid(invite)) {
            LOG_WARNING("Invite is expired or already processed");
            return false;
        }

        invite.accepted = true;
        it->second.accepted = true;
    }

    // 加入房间
    bool joined = joinRoom(invite.room_id, by_user_id);

    if (joined) {
        triggerEvent(FriendRoomEventType::INVITE_ACCEPTED, invite.room_id, by_user_id,
                      {{"invite_id", invite_id}});
        LOG_INFO("User accepted invite");
    }

    return joined;
}

bool FriendRoomManager::declineInvite(const std::string& invite_id, const std::string& by_user_id) {
    std::lock_guard<std::mutex> lock(invites_mutex_);

    auto it = invites_.find(invite_id);
    if (it == invites_.end()) {
        return false;
    }

    auto& invite = it->second;
    if (invite.target_user_id != by_user_id) {
        return false;
    }

    invite.declined = true;

    triggerEvent(FriendRoomEventType::INVITE_DECLINED, invite.room_id, by_user_id,
                  {{"invite_id", invite_id}});

    LOG_INFO("User declined invite");
    return true;
}

bool FriendRoomManager::cancelInvite(const std::string& invite_id, const std::string& by_user_id) {
    std::lock_guard<std::mutex> lock(invites_mutex_);

    auto it = invites_.find(invite_id);
    if (it == invites_.end()) {
        return false;
    }

    auto& invite = it->second;

    // 只有房主可以取消邀请
    if (invite.host_user_id != by_user_id) {
        return false;
    }

    // 通知被邀请者
    notifyUser(invite.target_user_id, "friend_room_invite_cancelled",
               {{"invite_id", invite_id}, {"room_id", invite.room_id}});

    // 移除邀请
    auto& user_list = user_invites_[invite.target_user_id];
    user_list.erase(
        std::remove(user_list.begin(), user_list.end(), invite_id),
        user_list.end());

    auto& room_list = room_invites_[invite.room_id];
    room_list.erase(
        std::remove(room_list.begin(), room_list.end(), invite_id),
        room_list.end());

    invites_.erase(it);

    LOG_INFO("User cancelled invite");
    return true;
}

std::vector<FriendRoomInvite> FriendRoomManager::getPendingInvites(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(invites_mutex_);

    std::vector<FriendRoomInvite> result;
    auto it = user_invites_.find(user_id);
    if (it != user_invites_.end()) {
        for (const auto& invite_id : it->second) {
            auto invite_it = invites_.find(invite_id);
            if (invite_it != invites_.end() && isInviteValid(invite_it->second)) {
                result.push_back(invite_it->second);
            }
        }
    }
    return result;
}

std::vector<FriendRoomInvite> FriendRoomManager::getRoomInvites(const std::string& room_id) const {
    std::lock_guard<std::mutex> lock(invites_mutex_);

    std::vector<FriendRoomInvite> result;
    auto it = room_invites_.find(room_id);
    if (it != room_invites_.end()) {
        for (const auto& invite_id : it->second) {
            auto invite_it = invites_.find(invite_id);
            if (invite_it != invites_.end()) {
                result.push_back(invite_it->second);
            }
        }
    }
    return result;
}

bool FriendRoomManager::joinRoom(const std::string& room_id, const std::string& user_id) {
    if (!canJoinRoom(room_id, user_id)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(rooms_mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return false;
    }

    auto& room = it->second;

    // 检查房间是否已满
    if (room.player_ids.size() >= 2) {
        // 作为观战者加入
        if (room.config.allow_spectators &&
            room.spectator_ids.size() < static_cast<size_t>(room.config.max_spectators)) {
            room.spectator_ids.push_back(user_id);
            user_to_room_[user_id] = room_id;

            triggerEvent(FriendRoomEventType::SPECTATOR_JOINED, room_id, user_id);
            LOG_INFO("User joined room as spectator");
            return true;
        }
        return false;
    }

    // 作为玩家加入
    room.player_ids.push_back(user_id);
    room.player_ready_status[user_id] = false;
    user_to_room_[user_id] = room_id;

    triggerEvent(FriendRoomEventType::PLAYER_JOINED, room_id, user_id);
    LOG_INFO("User joined room as player");
    return true;
}

bool FriendRoomManager::leaveRoom(const std::string& room_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(rooms_mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return false;
    }

    auto& room = it->second;

    // 房主不能离开房间
    if (room.config.host_user_id == user_id) {
        LOG_WARNING("Room host cannot leave room");
        return false;
    }

    // 从玩家列表移除
    auto player_it = std::find(room.player_ids.begin(), room.player_ids.end(), user_id);
    if (player_it != room.player_ids.end()) {
        room.player_ids.erase(player_it);
        room.player_ready_status.erase(user_id);
        user_to_room_.erase(user_id);

        triggerEvent(FriendRoomEventType::PLAYER_LEFT, room_id, user_id);
        LOG_INFO("User left room");
        return true;
    }

    // 从观战者列表移除
    auto spec_it = std::find(room.spectator_ids.begin(), room.spectator_ids.end(), user_id);
    if (spec_it != room.spectator_ids.end()) {
        room.spectator_ids.erase(spec_it);
        user_to_room_.erase(user_id);

        triggerEvent(FriendRoomEventType::SPECTATOR_LEFT, room_id, user_id);
        LOG_INFO("Spectator left room");
        return true;
    }

    return false;
}

bool FriendRoomManager::setReady(const std::string& room_id, const std::string& user_id, bool ready) {
    std::lock_guard<std::mutex> lock(rooms_mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return false;
    }

    auto& room = it->second;

    // 房主不需要设置准备状态
    if (room.config.host_user_id == user_id) {
        return false;
    }

    auto ready_it = room.player_ready_status.find(user_id);
    if (ready_it == room.player_ready_status.end()) {
        return false;
    }

    ready_it->second = ready;

    triggerEvent(FriendRoomEventType::PLAYER_READY, room_id, user_id, {{"ready", ready}});
    LOG_INFO("User set ready status");
    return true;
}

bool FriendRoomManager::canJoinRoom(const std::string& room_id, const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(rooms_mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return false;
    }

    const auto& room = it->second;

    // 检查是否已在房间中
    if (std::find(room.player_ids.begin(), room.player_ids.end(), user_id) != room.player_ids.end()) {
        return false;
    }

    // 根据房间类型检查权限
    switch (room.config.room_type) {
        case FriendRoomType::INVITE_ONLY:
            // 检查是否被邀请
            {
                std::lock_guard<std::mutex> invite_lock(invites_mutex_);
                auto invites_it = room_invites_.find(room_id);
                if (invites_it != room_invites_.end()) {
                    for (const auto& invite_id : invites_it->second) {
                        auto invite = invites_.find(invite_id);
                        if (invite != invites_.end() &&
                            invite->second.target_user_id == user_id &&
                            invite->second.accepted) {
                            return true;
                        }
                    }
                }
            }
            return false;

        case FriendRoomType::FRIENDS_ONLY:
            // 检查是否是房主的好友
            if (friend_service_) {
                return friend_service_->areFriends(room.config.host_user_id, user_id);
            }
            return true; // 无好友服务，允许加入

        case FriendRoomType::FRIENDS_OF_FRIENDS:
            // TODO: 实现好友的好友检查
            return true;
    }

    return false;
}

bool FriendRoomManager::joinAsSpectator(const std::string& room_id, const std::string& user_id) {
    std::lock_guard<std::mutex> lock(rooms_mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        return false;
    }

    auto& room = it->second;

    if (!room.config.allow_spectators) {
        return false;
    }

    if (room.spectator_ids.size() >= static_cast<size_t>(room.config.max_spectators)) {
        return false;
    }

    // 检查是否已在房间中
    if (std::find(room.spectator_ids.begin(), room.spectator_ids.end(), user_id) != room.spectator_ids.end()) {
        return false;
    }

    room.spectator_ids.push_back(user_id);
    user_to_room_[user_id] = room_id;

    triggerEvent(FriendRoomEventType::SPECTATOR_JOINED, room_id, user_id);
    LOG_INFO("User joined room as spectator");
    return true;
}

bool FriendRoomManager::leaveAsSpectator(const std::string& room_id, const std::string& user_id) {
    return leaveRoom(room_id, user_id);
}

nlohmann::json FriendRoomManager::getStatistics() const {
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    std::lock_guard<std::mutex> rooms_lock(rooms_mutex_);
    std::lock_guard<std::mutex> invites_lock(invites_mutex_);

    return {
        {"enabled", config_.enabled},
        {"total_rooms", rooms_.size()},
        {"total_invites", invites_.size()},
        {"statistics", {
            {"total_rooms_created", total_rooms_created_},
            {"total_invites_sent", total_invites_sent_},
            {"total_games_played", total_games_played_}
        }}
    };
}

std::string FriendRoomManager::generateRoomId() const {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    return "friend_room_" + std::to_string(ms) + "_" + std::to_string(++counter);
}

std::string FriendRoomManager::generateInviteId() const {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    return "invite_" + std::to_string(ms) + "_" + std::to_string(++counter);
}

void FriendRoomManager::triggerEvent(
    FriendRoomEventType type,
    const std::string& room_id,
    const std::string& user_id,
    const nlohmann::json& data) {

    if (event_callback_) {
        FriendRoomEvent event(type, room_id, user_id);
        event.data = data;
        event_callback_(event);
    }
}

void FriendRoomManager::cleanupExpiredInvites() {
    std::lock_guard<std::mutex> lock(invites_mutex_);

    auto now = std::chrono::system_clock::now();
    std::vector<std::string> expired_ids;

    for (const auto& [id, invite] : invites_) {
        if (now > invite.expires_at) {
            expired_ids.push_back(id);
            triggerEvent(FriendRoomEventType::INVITE_EXPIRED, invite.room_id, invite.target_user_id,
                          {{"invite_id", id}});
        }
    }

    for (const auto& id : expired_ids) {
        auto it = invites_.find(id);
        if (it != invites_.end()) {
            // 从用户邀请列表移除
            auto& user_invites_list = user_invites_[it->second.target_user_id];
            user_invites_list.erase(
                std::remove(user_invites_list.begin(), user_invites_list.end(), id),
                user_invites_list.end());

            // 从房间邀请列表移除
            auto& room_invites_list = room_invites_[it->second.room_id];
            room_invites_list.erase(
                std::remove(room_invites_list.begin(), room_invites_list.end(), id),
                room_invites_list.end());

            invites_.erase(it);
        }
    }
}

void FriendRoomManager::cleanupEmptyRooms() {
    std::lock_guard<std::mutex> lock(rooms_mutex_);

    auto now = std::chrono::system_clock::now();
    std::vector<std::string> rooms_to_remove;

    for (const auto& [id, room] : rooms_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
            now - room.created_at).count();

        // 空房间超过配置时间
        if (room.player_ids.empty() && elapsed > config_.room_expiry_minutes) {
            rooms_to_remove.push_back(id);
        }
    }

    for (const auto& id : rooms_to_remove) {
        rooms_.erase(id);
        LOG_INFO("Cleaned up empty room");
    }
}

bool FriendRoomManager::isInviteValid(const FriendRoomInvite& invite) const {
    if (invite.accepted || invite.declined) {
        return false;
    }

    auto now = std::chrono::system_clock::now();
    return now <= invite.expires_at;
}

bool FriendRoomManager::isRoomValid(const FriendRoomState& room) const {
    return !room.room_id.empty() && !room.config.host_user_id.empty();
}

void FriendRoomManager::notifyUser(
    const std::string& user_id,
    const std::string& type,
    const nlohmann::json& data) {

    if (friend_service_) {
        friend_service_->notifyUser(user_id, type, data);
    }
}

} // namespace gomoku
} // namespace game_services
