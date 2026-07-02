#pragma once

/**
 * @file user_models.h
 * @brief 用户数据模型定义 - 符合推荐分层架构
 * @details 定义user_service_db中的核心数据结构
 * @author AI Assistant
 * @date 2025-09-19
 * @version 1.0.0
 */

#include <string>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>

namespace core_services {
namespace user_service {

/**
 * @brief 用户状态枚举
 * @note 与 auth_service 保持一致，0 = 未激活，1 = 活跃
 */
enum class UserStatus {
    INACTIVE = 0,    // 未激活
    ACTIVE = 1,      // 活跃/正常
    SUSPENDED = 2,   // 暂停
    BANNED = 3,      // 封禁
    DELETED = 4      // 已删除
};

/**
 * @brief 在线状态枚举
 */
enum class OnlineStatus {
    OFFLINE = 0,     // 离线
    ONLINE = 1,      // 在线
    AWAY = 2,        // 离开
    BUSY = 3         // 忙碌
};

/**
 * @brief 用户基础信息 (users表)
 * @details 用户核心数据，对应user_service_db.users表
 */
struct UserInfo {
    std::string user_id;                                    // 用户ID (主键)
    std::string username;                                   // 用户名 (唯一)
    std::string email;                                      // 邮箱 (唯一)
    std::string password_hash;                              // 密码哈希
    std::string salt;                                       // 密码盐值
    std::string phone;                                      // 手机号 (可选)
    UserStatus status = UserStatus::ACTIVE;                // 用户状态
    OnlineStatus online_status = OnlineStatus::OFFLINE;    // 在线状态
    int login_attempts = 0;                                 // 登录失败次数
    std::optional<std::chrono::system_clock::time_point> locked_until;  // 锁定截止时间
    std::chrono::system_clock::time_point created_at;      // 创建时间
    std::chrono::system_clock::time_point updated_at;      // 更新时间
    std::optional<std::chrono::system_clock::time_point> last_login_at; // 最后登录时间
    std::string last_login_ip;                             // 最后登录IP

    // 验证方法
    std::string validate() const;

    // JSON序列化
    nlohmann::json toJson(bool include_sensitive = false) const;  // include_sensitive: 内部服务调用时设为true
    static UserInfo fromJson(const nlohmann::json& json);
    
    // 状态转换方法
    std::string statusToString() const;
    std::string onlineStatusToString() const;
    static UserStatus stringToStatus(const std::string& status);
    static OnlineStatus stringToOnlineStatus(const std::string& status);
};

/**
 * @brief 用户档案信息 (user_profiles表)
 * @details 用户扩展信息，对应user_service_db.user_profiles表
 */
struct UserProfile {
    std::string user_id;                                    // 用户ID (外键)
    std::string nickname;                                   // 昵称
    std::string real_name;                                  // 真实姓名 (可选)
    std::string avatar_url;                                 // 头像URL
    std::string bio;                                        // 个人简介
    std::string location;                                   // 地理位置
    std::string website;                                    // 个人网站
    std::optional<std::chrono::system_clock::time_point> birth_date; // 生日
    std::string gender;                                     // 性别 (M/F/O)
    std::string language = "zh-CN";                        // 首选语言
    std::string timezone = "Asia/Shanghai";                // 时区
    std::chrono::system_clock::time_point created_at;      // 创建时间
    std::chrono::system_clock::time_point updated_at;      // 更新时间
    
    // 验证方法
    std::string validate() const;
    
    // JSON序列化
    nlohmann::json toJson() const;
    static UserProfile fromJson(const nlohmann::json& json);
};

/**
 * @brief 用户偏好设置 (user_preferences表)
 * @details 用户个性化设置，对应user_service_db.user_preferences表
 */
struct UserPreferences {
    std::string user_id;                                    // 用户ID (外键)
    bool email_notifications = true;                       // 邮件通知
    bool push_notifications = true;                        // 推送通知
    bool sms_notifications = false;                        // 短信通知
    std::string theme = "light";                           // 主题 (light/dark)
    std::string language = "zh-CN";                        // 界面语言
    bool sound_effects = true;                             // 音效开关
    bool music = true;                                     // 音乐开关
    int volume = 80;                                       // 音量 (0-100)
    bool auto_login = false;                               // 自动登录
    bool remember_me = true;                               // 记住我
    int session_timeout = 3600;                           // 会话超时时间(秒)
    nlohmann::json custom_settings;                        // 自定义设置JSON
    std::chrono::system_clock::time_point created_at;      // 创建时间
    std::chrono::system_clock::time_point updated_at;      // 更新时间
    
    // 验证方法
    std::string validate() const;
    
    // JSON序列化
    nlohmann::json toJson() const;
    static UserPreferences fromJson(const nlohmann::json& json);
};

/**
 * @brief 用户会话信息 (user_sessions表)
 * @details 用户会话记录，对应user_service_db.user_sessions表
 */
struct UserSession {
    std::string session_id;                                 // 会话ID (主键)
    std::string user_id;                                    // 用户ID (外键)
    std::string device_id;                                  // 设备ID
    std::string device_type;                                // 设备类型
    std::string client_ip;                                  // 客户端IP
    std::string user_agent;                                 // 用户代理
    bool is_active = true;                                  // 是否活跃
    std::chrono::system_clock::time_point created_at;       // 创建时间
    std::chrono::system_clock::time_point updated_at;       // 更新时间
    std::optional<std::chrono::system_clock::time_point> expired_at; // 过期时间
    
    // 验证方法
    std::string validate() const;
    
    // JSON序列化
    nlohmann::json toJson() const;
    static UserSession fromJson(const nlohmann::json& json);
    
    // 状态检查
    bool isExpired() const;
    bool isValid() const;
};

/**
 * @brief 用户创建请求
 */
struct CreateUserRequest {
    std::string username;                                   // 用户名
    std::string email;                                      // 邮箱
    std::string password;                                   // 密码
    std::string phone;                                      // 手机号 (可选)
    std::string nickname;                                   // 昵称 (可选)
    std::string client_ip;                                  // 客户端IP
    std::string user_agent;                                 // 用户代理
    
    // 验证方法
    std::string validate() const;
    
    // JSON序列化
    nlohmann::json toJson() const;
    static CreateUserRequest fromJson(const nlohmann::json& json);
};

/**
 * @brief 用户更新请求
 */
struct UpdateUserRequest {
    std::optional<std::string> email;                       // 邮箱
    std::optional<std::string> phone;                       // 手机号
    std::optional<UserStatus> status;                      // 用户状态
    std::optional<OnlineStatus> online_status;             // 在线状态
    
    // 验证方法
    std::string validate() const;
    
    // JSON序列化
    nlohmann::json toJson() const;
    static UpdateUserRequest fromJson(const nlohmann::json& json);
};

/**
 * @brief 用户查询结果
 */
struct UserQueryResult {
    bool is_success = false;                                // 是否成功
    std::string error_message;                              // 错误消息
    std::string error_code;                                 // 错误代码
    std::optional<UserInfo> user_info;                     // 用户信息
    std::optional<UserProfile> user_profile;               // 用户档案
    std::optional<UserPreferences> user_preferences;       // 用户偏好
    
    // 创建成功结果
    static UserQueryResult success(const UserInfo& user_info);
    static UserQueryResult success(const UserInfo& user_info, 
                                   const UserProfile& profile,
                                   const UserPreferences& preferences);
    
    // 创建失败结果
    static UserQueryResult failure(const std::string& error_code, 
                                   const std::string& error_message);
    
    // JSON序列化
    nlohmann::json toJson() const;
};

// ==================== 辅助函数 ====================

/**
 * @brief 生成用户ID
 * @return 唯一用户ID
 */
std::string generateUserId();

/**
 * @brief 生成会话ID
 * @return 唯一会话ID
 */
std::string generateSessionId();

/**
 * @brief 验证邮箱格式
 * @param email 邮箱地址
 * @return 是否有效
 */
bool isValidEmail(const std::string& email);

/**
 * @brief 验证用户名格式
 * @param username 用户名
 * @return 是否有效
 */
bool isValidUsername(const std::string& username);

/**
 * @brief 验证手机号格式
 * @param phone 手机号
 * @return 是否有效
 */
bool isValidPhone(const std::string& phone);

/**
 * @brief 验证密码强度
 * @param password 密码
 * @return 验证结果和错误信息
 */
std::pair<bool, std::string> validatePassword(const std::string& password);

} // namespace user_service
} // namespace core_services




