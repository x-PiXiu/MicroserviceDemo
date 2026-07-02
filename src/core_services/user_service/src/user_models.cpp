/**
 * @file user_models.cpp
 * @brief 用户数据模型实现 - 符合推荐分层架构
 * @details 实现用户相关的数据模型类，支持JSON序列化和验证
 * @author AI Assistant
 * @date 2025-09-19
 * @version 1.0.0
 */

#include "include/user_models.h"
#include "common/logger/logger.h"
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <iomanip>

namespace core_services {
namespace user_service {

// ==================== UserInfo 实现 ====================

/**
 * @brief 默认构造函数
 */
// UserInfo::UserInfo() - 使用默认初始化列表而非显式构造函数 
//     : status("inactive")
//     , is_email_verified(false)
//     , is_phone_verified(false) {
// }

/**
 * @brief 验证用户信息
 * @return 验证错误信息，为空表示验证通过
 */
std::string UserInfo::validate() const {
    // 验证用户ID
    if (user_id.empty()) {
        return "用户ID不能为空";
    }
    
    // 验证用户名
    if (username.empty()) {
        return "用户名不能为空";
    }
    if (username.length() < 3 || username.length() > 50) {
        return "用户名长度必须在3-50个字符之间";
    }
    
    // 验证用户名格式（只允许字母、数字、下划线）
    std::regex username_pattern("^[a-zA-Z0-9_]+$");
    if (!std::regex_match(username, username_pattern)) {
        return "用户名只能包含字母、数字和下划线";
    }
    
    // 验证邮箱格式
    if (!email.empty()) {
        std::regex email_pattern(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
        if (!std::regex_match(email, email_pattern)) {
            return "邮箱格式不正确";
        }
    }
    
    // 验证手机号格式（中国手机号）
    if (!phone.empty()) {
        std::regex phone_pattern(R"(^1[3-9]\d{9}$)");
        if (!std::regex_match(phone, phone_pattern)) {
            return "手机号格式不正确";
        }
    }
    
    // 验证密码哈希
    if (password_hash.empty()) {
        return "密码哈希不能为空";
    }
    
    // 🔧 修复: BCrypt兼容性处理 - BCrypt是自包含salt的哈希算法
    // 检查是否为BCrypt格式的哈希（格式：$2a$, $2b$, $2x$, $2y$）
    bool is_bcrypt = password_hash.length() >= 4 && 
                    (password_hash.substr(0, 4) == "$2a$" || 
                     password_hash.substr(0, 4) == "$2b$" || 
                     password_hash.substr(0, 4) == "$2x$" || 
                     password_hash.substr(0, 4) == "$2y$");
    
    // 对于BCrypt哈希，salt可以为空；对于其他哈希算法，salt不能为空
    if (!is_bcrypt && salt.empty()) {
        return "非BCrypt算法的密码必须提供盐值";
    }
    
    // 用户状态验证由枚举类型保证，无需额外验证
    
    return "";  // 验证通过
}

/**
 * @brief 转换为JSON对象
 * @param include_sensitive 是否包含敏感字段（password_hash, salt），内部服务调用时设为true
 * @return JSON对象
 * @note 默认不包含敏感字段，仅在内部服务间调用时包含
 */
nlohmann::json UserInfo::toJson(bool include_sensitive) const {
    nlohmann::json json;

    json["user_id"] = user_id;
    json["username"] = username;
    json["email"] = email;

    // 仅在内部服务调用时包含敏感字段
    if (include_sensitive) {
        json["password_hash"] = password_hash;
        json["salt"] = salt;
    }

    json["phone"] = phone;
    json["status"] = statusToString();
    json["online_status"] = onlineStatusToString();
    json["login_attempts"] = login_attempts;
    json["last_login_ip"] = last_login_ip;
    // 时间字段转换为时间戳（秒）
    auto created_time_t = std::chrono::system_clock::to_time_t(created_at);
    auto updated_time_t = std::chrono::system_clock::to_time_t(updated_at);
    json["created_at"] = static_cast<int64_t>(created_time_t);
    json["updated_at"] = static_cast<int64_t>(updated_time_t);
    // 可选字段
    if (last_login_at.has_value()) {
        auto login_time_t = std::chrono::system_clock::to_time_t(last_login_at.value());
        json["last_login_at"] = static_cast<int64_t>(login_time_t);
    }
    if (locked_until.has_value()) {
        auto locked_time_t = std::chrono::system_clock::to_time_t(locked_until.value());
        json["locked_until"] = static_cast<int64_t>(locked_time_t);
    }

    return json;
}

/**
 * @brief 从JSON对象创建UserInfo
 * @param json JSON对象
 * @return UserInfo对象
 */
UserInfo UserInfo::fromJson(const nlohmann::json& json) {
    UserInfo user;
    
    if (json.contains("user_id") && json["user_id"].is_string()) {
        user.user_id = json["user_id"].get<std::string>();
    }
    if (json.contains("username") && json["username"].is_string()) {
        user.username = json["username"].get<std::string>();
    }
    if (json.contains("email") && json["email"].is_string()) {
        user.email = json["email"].get<std::string>();
    }
    
    // 🔧 修复: 添加密码哈希和盐值字段处理
    if (json.contains("password_hash") && json["password_hash"].is_string()) {
        user.password_hash = json["password_hash"].get<std::string>();
    }
    if (json.contains("salt") && json["salt"].is_string()) {
        user.salt = json["salt"].get<std::string>();
    }
    
    if (json.contains("phone") && json["phone"].is_string()) {
        user.phone = json["phone"].get<std::string>();
    }
    
    // 🔧 修复: 支持数字和字符串类型的状态字段
    if (json.contains("status")) {
        if (json["status"].is_string()) {
            user.status = stringToStatus(json["status"].get<std::string>());
        } else if (json["status"].is_number()) {
            int status_num = json["status"].get<int>();
            user.status = static_cast<UserStatus>(status_num);
        }
    }
    if (json.contains("online_status")) {
        if (json["online_status"].is_string()) {
            user.online_status = stringToOnlineStatus(json["online_status"].get<std::string>());
        } else if (json["online_status"].is_number()) {
            int online_status_num = json["online_status"].get<int>();
            user.online_status = static_cast<OnlineStatus>(online_status_num);
        }
    }
    if (json.contains("login_attempts") && json["login_attempts"].is_number()) {
        user.login_attempts = json["login_attempts"].get<int>();
    }
    if (json.contains("last_login_ip") && json["last_login_ip"].is_string()) {
        user.last_login_ip = json["last_login_ip"].get<std::string>();
    }
    // 时间字段从时间戳转换
    if (json.contains("created_at") && json["created_at"].is_number()) {
        auto timestamp = json["created_at"].get<int64_t>();
        user.created_at = std::chrono::system_clock::from_time_t(timestamp);
    }
    if (json.contains("updated_at") && json["updated_at"].is_number()) {
        auto timestamp = json["updated_at"].get<int64_t>();
        user.updated_at = std::chrono::system_clock::from_time_t(timestamp);
    }
    if (json.contains("last_login_at") && json["last_login_at"].is_number()) {
        auto timestamp = json["last_login_at"].get<int64_t>();
        user.last_login_at = std::chrono::system_clock::from_time_t(timestamp);
    }
    if (json.contains("locked_until") && json["locked_until"].is_number()) {
        auto timestamp = json["locked_until"].get<int64_t>();
        user.locked_until = std::chrono::system_clock::from_time_t(timestamp);
    }
    
    return user;
}

// ==================== UserProfile 实现 ====================

/**
 * @brief 默认构造函数
 */
// UserProfile::UserProfile() - 使用默认初始化列表
// UserProfile::UserProfile() 
//     : gender("unknown")
//     , timezone("Asia/Shanghai")
//     , language("zh-CN") {
// }

/**
 * @brief 验证用户档案
 * @return 验证错误信息，为空表示验证通过
 */
std::string UserProfile::validate() const {
    // 验证用户ID
    if (user_id.empty()) {
        return "用户ID不能为空";
    }
    
    // birth_date是std::optional<std::chrono::system_clock::time_point>，不需要字符串验证
    // 如果需要验证，可以检查 birth_date.has_value()
    
    // 验证性别
    if (gender != "male" && gender != "female" && gender != "other" && gender != "unknown") {
        return "性别值无效";
    }
    
    // 验证语言代码
    if (!language.empty()) {
        std::regex lang_pattern(R"([a-z]{2}-[A-Z]{2})");
        if (!std::regex_match(language, lang_pattern)) {
            return "语言代码格式不正确，应为xx-XX格式";
        }
    }
    
    // 验证简介长度
    if (bio.length() > 500) {
        return "个人简介不能超过500个字符";
    }
    
    return "";  // 验证通过
}

/**
 * @brief 转换为JSON对象
 * @return JSON对象
 */
nlohmann::json UserProfile::toJson() const {
    nlohmann::json json;
    
    json["user_id"] = user_id;
    json["nickname"] = nickname;
    json["real_name"] = real_name;
    json["avatar_url"] = avatar_url;
    json["bio"] = bio;
    json["location"] = location;
    json["website"] = website;
    json["gender"] = gender;
    json["language"] = language;
    json["timezone"] = timezone;
    
    // 处理可选生日字段
    if (birth_date.has_value()) {
        auto birth_time_t = std::chrono::system_clock::to_time_t(birth_date.value());
        json["birth_date"] = static_cast<int64_t>(birth_time_t);
    }
    
    // 时间字段转换为时间戳
    auto created_time_t = std::chrono::system_clock::to_time_t(created_at);
    auto updated_time_t = std::chrono::system_clock::to_time_t(updated_at);
    json["created_at"] = static_cast<int64_t>(created_time_t);
    json["updated_at"] = static_cast<int64_t>(updated_time_t);
    
    return json;
}

/**
 * @brief 从JSON对象创建UserProfile
 * @param json JSON对象
 * @return UserProfile对象
 */
UserProfile UserProfile::fromJson(const nlohmann::json& json) {
    UserProfile profile;
    
    if (json.contains("user_id") && json["user_id"].is_string()) {
        profile.user_id = json["user_id"].get<std::string>();
    }
    if (json.contains("nickname") && json["nickname"].is_string()) {
        profile.nickname = json["nickname"].get<std::string>();
    }
    if (json.contains("real_name") && json["real_name"].is_string()) {
        profile.real_name = json["real_name"].get<std::string>();
    }
    if (json.contains("avatar_url") && json["avatar_url"].is_string()) {
        profile.avatar_url = json["avatar_url"].get<std::string>();
    }
    if (json.contains("bio") && json["bio"].is_string()) {
        profile.bio = json["bio"].get<std::string>();
    }
    if (json.contains("location") && json["location"].is_string()) {
        profile.location = json["location"].get<std::string>();
    }
    if (json.contains("website") && json["website"].is_string()) {
        profile.website = json["website"].get<std::string>();
    }
    if (json.contains("gender") && json["gender"].is_string()) {
        profile.gender = json["gender"].get<std::string>();
    }
    if (json.contains("language") && json["language"].is_string()) {
        profile.language = json["language"].get<std::string>();
    }
    if (json.contains("timezone") && json["timezone"].is_string()) {
        profile.timezone = json["timezone"].get<std::string>();
    }
    // 时间字段从时间戳转换
    if (json.contains("birth_date") && json["birth_date"].is_number()) {
        auto timestamp = json["birth_date"].get<int64_t>();
        profile.birth_date = std::chrono::system_clock::from_time_t(timestamp);
    }
    if (json.contains("created_at") && json["created_at"].is_number()) {
        auto timestamp = json["created_at"].get<int64_t>();
        profile.created_at = std::chrono::system_clock::from_time_t(timestamp);
    }
    if (json.contains("updated_at") && json["updated_at"].is_number()) {
        auto timestamp = json["updated_at"].get<int64_t>();
        profile.updated_at = std::chrono::system_clock::from_time_t(timestamp);
    }
    
    return profile;
}

// ==================== UserPreferences 实现 ====================

// UserPreferences构造函数已在头文件中通过默认值初始化隐式定义
// 删除显式构造函数定义以避免重复定义错误

/**
 * @brief 验证用户偏好设置
 * @return 验证错误信息，为空表示验证通过
 */
std::string UserPreferences::validate() const {
    // 验证用户ID
    if (user_id.empty()) {
        return "用户ID不能为空";
    }
    
    // 验证主题
    if (theme != "default" && theme != "dark" && theme != "light" && theme != "auto") {
        return "主题值无效";
    }
    
    // 验证语言代码
    if (!language.empty()) {
        std::regex lang_pattern(R"([a-z]{2}-[A-Z]{2})");
        if (!std::regex_match(language, lang_pattern)) {
            return "语言代码格式不正确，应为xx-XX格式";
        }
    }
    
    // 验证音量范围
    if (volume < 0 || volume > 100) {
        return "音量值必须在0-100之间";
    }
    
    // 验证会话超时时间
    if (session_timeout < 300 || session_timeout > 86400) {  // 5分钟到24小时
        return "会话超时时间必须在300-86400秒之间";
    }
    
    // 验证自定义设置JSON格式
    if (!custom_settings.is_null() && !custom_settings.is_object()) {
        return "自定义设置必须是有效的JSON对象";
    }
    
    return "";  // 验证通过
}

/**
 * @brief 转换为JSON对象
 * @return JSON对象
 */
nlohmann::json UserPreferences::toJson() const {
    nlohmann::json json;
    json["user_id"] = user_id;
    json["email_notifications"] = email_notifications;
    json["push_notifications"] = push_notifications;
    json["sms_notifications"] = sms_notifications;
    json["theme"] = theme;
    json["language"] = language;
    json["sound_effects"] = sound_effects;
    json["music"] = music;
    json["volume"] = volume;
    json["auto_login"] = auto_login;
    json["remember_me"] = remember_me;
    json["session_timeout"] = session_timeout;
    json["custom_settings"] = custom_settings;
    
    // 时间字段转换为时间戳
    auto created_time_t = std::chrono::system_clock::to_time_t(created_at);
    auto updated_time_t = std::chrono::system_clock::to_time_t(updated_at);
    json["created_at"] = static_cast<int64_t>(created_time_t);
    json["updated_at"] = static_cast<int64_t>(updated_time_t);
    
    return json;
}

/**
 * @brief 从JSON对象创建UserPreference
 * @param json JSON对象
 * @return UserPreference对象
 */
UserPreferences UserPreferences::fromJson(const nlohmann::json& json) {
    UserPreferences preference;
    
    if (json.contains("user_id") && json["user_id"].is_string()) {
        preference.user_id = json["user_id"].get<std::string>();
    }
    if (json.contains("email_notifications") && json["email_notifications"].is_boolean()) {
        preference.email_notifications = json["email_notifications"].get<bool>();
    }
    if (json.contains("push_notifications") && json["push_notifications"].is_boolean()) {
        preference.push_notifications = json["push_notifications"].get<bool>();
    }
    if (json.contains("sms_notifications") && json["sms_notifications"].is_boolean()) {
        preference.sms_notifications = json["sms_notifications"].get<bool>();
    }
    if (json.contains("theme") && json["theme"].is_string()) {
        preference.theme = json["theme"].get<std::string>();
    }
    if (json.contains("language") && json["language"].is_string()) {
        preference.language = json["language"].get<std::string>();
    }
    if (json.contains("sound_effects") && json["sound_effects"].is_boolean()) {
        preference.sound_effects = json["sound_effects"].get<bool>();
    }
    if (json.contains("music") && json["music"].is_boolean()) {
        preference.music = json["music"].get<bool>();
    }
    if (json.contains("volume") && json["volume"].is_number()) {
        preference.volume = json["volume"].get<int>();
    }
    if (json.contains("auto_login") && json["auto_login"].is_boolean()) {
        preference.auto_login = json["auto_login"].get<bool>();
    }
    if (json.contains("remember_me") && json["remember_me"].is_boolean()) {
        preference.remember_me = json["remember_me"].get<bool>();
    }
    if (json.contains("session_timeout") && json["session_timeout"].is_number()) {
        preference.session_timeout = json["session_timeout"].get<int>();
    }
    if (json.contains("custom_settings") && json["custom_settings"].is_object()) {
        preference.custom_settings = json["custom_settings"];
    }
    // 时间字段从时间戳转换
    if (json.contains("created_at") && json["created_at"].is_number()) {
        auto timestamp = json["created_at"].get<int64_t>();
        preference.created_at = std::chrono::system_clock::from_time_t(timestamp);
    }
    if (json.contains("updated_at") && json["updated_at"].is_number()) {
        auto timestamp = json["updated_at"].get<int64_t>();
        preference.updated_at = std::chrono::system_clock::from_time_t(timestamp);
    }
    
    return preference;
}

// ==================== UserSession 实现 ====================

/**
 * @brief 默认构造函数
 */
// UserSession::UserSession() - 使用默认初始化列表
// UserSession::UserSession()
//     : is_active(true) {
// }

/**
 * @brief 验证用户会话
 * @return 验证错误信息，为空表示验证通过
 */
std::string UserSession::validate() const {
    // 验证会话ID
    if (session_id.empty()) {
        return "会话ID不能为空";
    }
    
    // 验证用户ID
    if (user_id.empty()) {
        return "用户ID不能为空";
    }
    
    // UserSession中没有access_token和refresh_token字段
    // 只有session_id, user_id, device_id等字段
    // expired_at是std::optional<std::chrono::system_clock::time_point>类型
    
    return "";  // 验证通过
}

/**
 * @brief 转换为JSON对象
 * @return JSON对象
 */
nlohmann::json UserSession::toJson() const {
    nlohmann::json json;
    
    json["session_id"] = session_id;
    json["user_id"] = user_id;
    json["device_id"] = device_id;
    json["device_type"] = device_type;
    json["client_ip"] = client_ip;
    json["user_agent"] = user_agent;
    json["is_active"] = is_active;
    
    // 时间字段转换为时间戳
    auto created_time_t = std::chrono::system_clock::to_time_t(created_at);
    auto updated_time_t = std::chrono::system_clock::to_time_t(updated_at);
    json["created_at"] = static_cast<int64_t>(created_time_t);
    json["updated_at"] = static_cast<int64_t>(updated_time_t);
    
    // 可选过期时间
    if (expired_at.has_value()) {
        auto expired_time_t = std::chrono::system_clock::to_time_t(expired_at.value());
        json["expired_at"] = static_cast<int64_t>(expired_time_t);
    }
    
    return json;
}

/**
 * @brief 从JSON对象创建UserSession
 * @param json JSON对象
 * @return UserSession对象
 */
UserSession UserSession::fromJson(const nlohmann::json& json) {
    UserSession session;
    
    if (json.contains("session_id") && json["session_id"].is_string()) {
        session.session_id = json["session_id"].get<std::string>();
    }
    if (json.contains("user_id") && json["user_id"].is_string()) {
        session.user_id = json["user_id"].get<std::string>();
    }
    if (json.contains("device_id") && json["device_id"].is_string()) {
        session.device_id = json["device_id"].get<std::string>();
    }
    if (json.contains("device_type") && json["device_type"].is_string()) {
        session.device_type = json["device_type"].get<std::string>();
    }
    if (json.contains("client_ip") && json["client_ip"].is_string()) {
        session.client_ip = json["client_ip"].get<std::string>();
    }
    if (json.contains("user_agent") && json["user_agent"].is_string()) {
        session.user_agent = json["user_agent"].get<std::string>();
    }
    if (json.contains("is_active") && json["is_active"].is_boolean()) {
        session.is_active = json["is_active"].get<bool>();
    }
    
    // 处理时间戳转换
    if (json.contains("created_at") && json["created_at"].is_number()) {
        int64_t timestamp = json["created_at"].get<int64_t>();
        session.created_at = std::chrono::system_clock::from_time_t(static_cast<time_t>(timestamp));
    }
    
    if (json.contains("updated_at") && json["updated_at"].is_number()) {
        int64_t timestamp = json["updated_at"].get<int64_t>();
        session.updated_at = std::chrono::system_clock::from_time_t(static_cast<time_t>(timestamp));
    }
    
    // 处理可选的过期时间
    if (json.contains("expired_at") && json["expired_at"].is_number()) {
        int64_t timestamp = json["expired_at"].get<int64_t>();
        session.expired_at = std::chrono::system_clock::from_time_t(static_cast<time_t>(timestamp));
    }
    
    return session;
}

// ==================== UserInfo 状态转换方法实现 ====================

/**
 * @brief 将用户状态转换为字符串
 */
std::string UserInfo::statusToString() const {
    switch (status) {
        case UserStatus::ACTIVE: return "active";
        case UserStatus::INACTIVE: return "inactive";
        case UserStatus::SUSPENDED: return "suspended";
        case UserStatus::BANNED: return "banned";
        case UserStatus::DELETED: return "deleted";
        default: return "unknown";
    }
}

/**
 * @brief 将在线状态转换为字符串
 */
std::string UserInfo::onlineStatusToString() const {
    switch (online_status) {
        case OnlineStatus::OFFLINE: return "offline";
        case OnlineStatus::ONLINE: return "online";
        case OnlineStatus::AWAY: return "away";
        case OnlineStatus::BUSY: return "busy";
        default: return "unknown";
    }
}

/**
 * @brief 将字符串转换为用户状态
 */
UserStatus UserInfo::stringToStatus(const std::string& status) {
    if (status == "active") return UserStatus::ACTIVE;
    if (status == "inactive") return UserStatus::INACTIVE;
    if (status == "suspended") return UserStatus::SUSPENDED;
    if (status == "banned") return UserStatus::BANNED;
    if (status == "deleted") return UserStatus::DELETED;
    return UserStatus::INACTIVE;  // 默认为未激活状态
}

/**
 * @brief 将字符串转换为在线状态
 */
OnlineStatus UserInfo::stringToOnlineStatus(const std::string& status) {
    if (status == "offline") return OnlineStatus::OFFLINE;
    if (status == "online") return OnlineStatus::ONLINE;
    if (status == "away") return OnlineStatus::AWAY;
    if (status == "busy") return OnlineStatus::BUSY;
    return OnlineStatus::OFFLINE;  // 默认为离线状态
}

} // namespace user_service
} // namespace core_services




