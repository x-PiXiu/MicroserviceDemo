/**
 * @file ip_blacklist_manager.h
 * @brief IP黑名单管理器 - 支持IPv4和IPv6的全局黑名单功能
 * @author AI Assistant
 * @date 2025-12-07
 * @version 1.0
 * 
 * 功能特性:
 * - 支持IPv4和IPv6黑名单
 * - 支持单IP、IP范围、CIDR格式
 * - 线程安全的动态添加/删除
 * - 持久化存储（可选）
 * - 快速查询（O(log n)）
 * - 支持黑名单统计和查询
 */

#ifndef IP_BLACKLIST_MANAGER_H
#define IP_BLACKLIST_MANAGER_H

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>
#include "ip_utils.h"

namespace common {
namespace network {
    class EventLoop;  // 前向声明
}

namespace utils {

/**
 * @brief 黑名单条目类型
 */
enum class BlacklistEntryType {
    SINGLE_IP_V4,      // 单个IPv4地址
    SINGLE_IP_V6,      // 单个IPv6地址
    RANGE_IP_V4,       // IPv4范围
    CIDR_IP_V4,        // IPv4 CIDR
    CIDR_IP_V6         // IPv6 CIDR（简化支持）
};

/**
 * @brief 黑名单条目信息
 */
struct BlacklistEntry {
    std::string id;                                 ///< 唯一标识符
    BlacklistEntryType type;                        ///< 条目类型
    std::string ip_pattern;                         ///< 原始IP模式
    std::string ip_range_start;                     ///< 范围起始IP（仅范围类型）
    std::string ip_range_end;                       ///< 范围结束IP（仅范围类型）
    std::string reason;                             ///< 加入黑名单原因
    std::chrono::system_clock::time_point created_at; ///< 创建时间
    std::chrono::system_clock::time_point expires_at; ///< 过期时间（可选，0表示永不过期）
    bool is_permanent;                              ///< 是否永久黑名单
    
    /**
     * @brief 转换为JSON格式
     */
    nlohmann::json toJson() const;
    
    /**
     * @brief 从JSON创建条目
     */
    static BlacklistEntry fromJson(const nlohmann::json& json);
};

/**
 * @brief IP黑名单管理器（单例模式）
 */
class IPBlacklistManager {
public:
    /**
     * @brief 获取单例实例
     */
    static IPBlacklistManager& getInstance();
    
    // 禁止拷贝和赋值
    IPBlacklistManager(const IPBlacklistManager&) = delete;
    IPBlacklistManager& operator=(const IPBlacklistManager&) = delete;
    
    /**
     * @brief 初始化黑名单管理器
     * @param storage_file 黑名单存储文件路径（可选）
     * @return 初始化成功返回true
     */
    bool initialize(const std::string& storage_file = "");
    
    /**
     * @brief 添加IP到黑名单
     * @param ip_pattern IP模式（支持单IP、范围、CIDR）
     * @param reason 加入原因
     * @param duration_seconds 持续时间（秒，0表示永久）
     * @return 成功返回条目ID，失败返回空字符串
     */
    std::string addToBlacklist(const std::string& ip_pattern, 
                              const std::string& reason = "Manual block",
                              int duration_seconds = 0);
    
    /**
     * @brief 从黑名单中移除IP
     * @param entry_id 条目ID或IP模式
     * @return 成功返回true
     */
    bool removeFromBlacklist(const std::string& entry_id);
    
    /**
     * @brief 检查IP是否在黑名单中
     * @param ip IP地址（IPv4或IPv6）
     * @return 在黑名单中返回true
     */
    bool isBlacklisted(const std::string& ip) const;
    
    /**
     * @brief 获取所有黑名单条目
     * @return 黑名单条目列表
     */
    std::vector<BlacklistEntry> getAllEntries() const;
    
    /**
     * @brief 获取黑名单统计信息
     * @return JSON格式的统计信息
     */
    nlohmann::json getStatistics() const;
    
    /**
     * @brief 清空所有黑名单
     */
    void clearAll();
    
    /**
     * @brief 清理过期条目
     * @return 清理的条目数量
     */
    size_t cleanupExpiredEntries();
    
    /**
     * @brief 保存黑名单到文件
     * @return 成功返回true
     */
    bool save();
    
    /**
     * @brief 从文件加载黑名单
     * @return 成功返回true
     */
    bool load();
    
    /**
     * 启用自动清理过期条目（使用EventLoop的时间轮）
     * @param event_loop EventLoop指针，用于注册定时任务
     * @param interval_seconds 清理间隔（秒）
     */
    void enableAutoCleanup(common::network::EventLoop* event_loop, int interval_seconds = 3600);
    
    /**
     * 禁用自动清理
     */
    void disableAutoCleanup();
    
private:
    IPBlacklistManager();
    ~IPBlacklistManager();
    
    // ==================== 内部数据结构 ====================
    
    /// IPv4单IP黑名单（使用uint32_t快速查询）
    std::unordered_set<uint32_t> ipv4_single_ips_;
    
    /// IPv6单IP黑名单（使用字符串存储）
    std::unordered_set<std::string> ipv6_single_ips_;
    
    /// IPv4范围黑名单（使用pair<start, end>存储，有序）
    struct IPv4Range {
        uint32_t start;
        uint32_t end;
        std::string entry_id;
        
        bool contains(uint32_t ip) const {
            return ip >= start && ip <= end;
        }
    };
    std::vector<IPv4Range> ipv4_ranges_;
    
    /// 黑名单条目映射（entry_id -> BlacklistEntry）
    std::unordered_map<std::string, BlacklistEntry> entries_;
    
    /// IP模式到entry_id的反向索引
    std::unordered_map<std::string, std::string> pattern_to_id_;
    
    // ==================== 同步和存储 ====================
    
    mutable std::shared_mutex mutex_;               ///< 读写锁
    std::string storage_file_;                      ///< 存储文件路径
    
    // ⭐⭐⭐ 使用EventLoop时间轮的自动清理
    common::network::EventLoop* event_loop_{nullptr}; ///< EventLoop指针（用于注册定时任务）
    uint64_t cleanup_timer_id_{0};                  ///< 清理定时器ID（用于取消任务）
    std::atomic<bool> auto_cleanup_enabled_{false}; ///< 自动清理标志
    
    // ==================== 内部辅助方法 ====================
    
    /**
     * @brief 生成唯一条目ID
     */
    std::string generateEntryId() const;
    
    /**
     * @brief 添加IPv4单IP到黑名单
     */
    void addIPv4Single(uint32_t ip, const std::string& entry_id);
    
    /**
     * @brief 添加IPv6单IP到黑名单
     */
    void addIPv6Single(const std::string& ip, const std::string& entry_id);
    
    /**
     * @brief 添加IPv4范围到黑名单
     */
    void addIPv4Range(uint32_t start, uint32_t end, const std::string& entry_id);
    
    /**
     * @brief 检查IPv4是否在黑名单中
     */
    bool isIPv4Blacklisted(uint32_t ip) const;
    
    /**
     * @brief 检查IPv6是否在黑名单中
     */
    bool isIPv6Blacklisted(const std::string& ip) const;
    
    /**
     * @brief 移除条目的内部实现（需要持有写锁）
     */
    void removeEntryInternal(const BlacklistEntry& entry);
};

} // namespace utils
} // namespace common

#endif // IP_BLACKLIST_MANAGER_H
