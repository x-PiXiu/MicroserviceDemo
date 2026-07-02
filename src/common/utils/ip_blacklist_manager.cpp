/**
 * @file ip_blacklist_manager.cpp
 * @brief IP黑名单管理器实现
 */

#include "common/utils/ip_blacklist_manager.h"
#include "common/logger/logger.h"
#include "common/network/event_loop.h"  // ⭐⭐⭐ 新增：使用EventLoop
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <arpa/inet.h>

namespace common {
namespace utils {

// ==================== BlacklistEntry实现 ====================

nlohmann::json BlacklistEntry::toJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["type"] = static_cast<int>(type);
    j["ip_pattern"] = ip_pattern;
    j["ip_range_start"] = ip_range_start;
    j["ip_range_end"] = ip_range_end;
    j["reason"] = reason;
    j["is_permanent"] = is_permanent;
    
    // ⭐⭐⭐ 修复：使用本地时间而不是UTC时间
    auto created_time_t = std::chrono::system_clock::to_time_t(created_at);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&created_time_t), "%Y-%m-%dT%H:%M:%S");  // 改为localtime
    j["created_at"] = oss.str();
    
    if (!is_permanent) {
        auto expires_time_t = std::chrono::system_clock::to_time_t(expires_at);
        std::ostringstream oss_exp;
        oss_exp << std::put_time(std::localtime(&expires_time_t), "%Y-%m-%dT%H:%M:%S");  // 改为localtime
        j["expires_at"] = oss_exp.str();
    } else {
        j["expires_at"] = nullptr;
    }
    
    return j;
}

BlacklistEntry BlacklistEntry::fromJson(const nlohmann::json& json) {
    BlacklistEntry entry;
    entry.id = json.value("id", "");
    entry.type = static_cast<BlacklistEntryType>(json.value("type", 0));
    entry.ip_pattern = json.value("ip_pattern", "");
    entry.ip_range_start = json.value("ip_range_start", "");
    entry.ip_range_end = json.value("ip_range_end", "");
    entry.reason = json.value("reason", "");
    entry.is_permanent = json.value("is_permanent", true);
    
    // 解析时间戳（简化处理，实际应该用更robust的解析）
    entry.created_at = std::chrono::system_clock::now();
    entry.expires_at = std::chrono::system_clock::time_point::max();
    
    return entry;
}

// ==================== IPBlacklistManager实现 ====================

IPBlacklistManager::IPBlacklistManager() {
    LOG_INFO("IPBlacklistManager created");
}

IPBlacklistManager::~IPBlacklistManager() {
    disableAutoCleanup();
    save();
    LOG_INFO("IPBlacklistManager destroyed");
}

IPBlacklistManager& IPBlacklistManager::getInstance() {
    static IPBlacklistManager instance;
    return instance;
}

bool IPBlacklistManager::initialize(const std::string& storage_file) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    storage_file_ = storage_file;
    
    if (!storage_file_.empty()) {
        // 尝试加载现有黑名单
        lock.unlock();  // 解锁后调用load()，因为load()会自己加锁
        if (load()) {
            LOG_INFO("Loaded blacklist from file: " + storage_file_);
        } else {
            LOG_WARNING("Failed to load blacklist from file, starting with empty blacklist");
        }
    }
    
    LOG_INFO("IPBlacklistManager initialized");
    return true;
}

std::string IPBlacklistManager::generateEntryId() const {
    // 生成唯一ID：timestamp + random
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    return "bl_" + std::to_string(ms) + "_" + std::to_string(dis(gen));
}

std::string IPBlacklistManager::addToBlacklist(const std::string& ip_pattern, 
                                              const std::string& reason,
                                              int duration_seconds) {
    try {
        // 解析IP模式
        IPWhitelistInfo ip_info = IPUtils::parseIPAddress(ip_pattern);
        
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        // 检查是否已存在
        auto it = pattern_to_id_.find(ip_pattern);
        if (it != pattern_to_id_.end()) {
            LOG_WARNING("IP pattern already in blacklist: " + ip_pattern);
            return it->second;  // 返回已存在的条目ID
        }
        
        // 创建新条目
        BlacklistEntry entry;
        entry.id = generateEntryId();
        entry.ip_pattern = ip_pattern;
        entry.reason = reason;
        entry.created_at = std::chrono::system_clock::now();
        entry.is_permanent = (duration_seconds == 0);
        
        if (!entry.is_permanent) {
            entry.expires_at = entry.created_at + std::chrono::seconds(duration_seconds);
        } else {
            entry.expires_at = std::chrono::system_clock::time_point::max();
        }
        
        // 根据类型添加到相应的数据结构
        switch (ip_info.type) {
            case IPWhitelistType::SINGLE_IP: {
                // 判断是IPv4还是IPv6
                if (IPUtils::isValidIPv4(ip_pattern)) {
                    entry.type = BlacklistEntryType::SINGLE_IP_V4;
                    uint32_t ip_int = IPUtils::ipToUint32(ip_pattern);
                    addIPv4Single(ip_int, entry.id);
                } else if (IPUtils::isValidIPv6(ip_pattern)) {
                    entry.type = BlacklistEntryType::SINGLE_IP_V6;
                    addIPv6Single(ip_pattern, entry.id);
                }
                break;
            }
            
            case IPWhitelistType::RANGE_IP: {
                entry.type = BlacklistEntryType::RANGE_IP_V4;
                entry.ip_range_start = ip_info.ip_range_start;
                entry.ip_range_end = ip_info.ip_range_end;
                
                uint32_t start_int = IPUtils::ipToUint32(ip_info.ip_range_start);
                uint32_t end_int = IPUtils::ipToUint32(ip_info.ip_range_end);
                addIPv4Range(start_int, end_int, entry.id);
                break;
            }
            
            case IPWhitelistType::CIDR_IP: {
                entry.type = BlacklistEntryType::CIDR_IP_V4;
                entry.ip_range_start = ip_info.ip_range_start;
                entry.ip_range_end = ip_info.ip_range_end;
                
                uint32_t start_int = IPUtils::ipToUint32(ip_info.ip_range_start);
                uint32_t end_int = IPUtils::ipToUint32(ip_info.ip_range_end);
                addIPv4Range(start_int, end_int, entry.id);
                break;
            }
        }
        
        // 保存条目
        entries_[entry.id] = entry;
        pattern_to_id_[ip_pattern] = entry.id;
        
        LOG_INFO("Added to blacklist: " + ip_pattern + " (ID: " + entry.id + ", Reason: " + reason + ")");
        
        // 自动保存
        if (!storage_file_.empty()) {
            lock.unlock();
            save();
        }
        
        return entry.id;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to add to blacklist: " + std::string(e.what()));
        return "";
    }
}

bool IPBlacklistManager::removeFromBlacklist(const std::string& entry_id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 首先尝试作为entry_id查找
    auto it = entries_.find(entry_id);
    if (it != entries_.end()) {
        removeEntryInternal(it->second);
        entries_.erase(it);
        LOG_INFO("Removed from blacklist by ID: " + entry_id);
        
        // 自动保存
        if (!storage_file_.empty()) {
            lock.unlock();
            save();
        }
        return true;
    }
    
    // 尝试作为IP模式查找
    auto pattern_it = pattern_to_id_.find(entry_id);
    if (pattern_it != pattern_to_id_.end()) {
        std::string real_id = pattern_it->second;
        auto entry_it = entries_.find(real_id);
        if (entry_it != entries_.end()) {
            removeEntryInternal(entry_it->second);
            entries_.erase(entry_it);
            LOG_INFO("Removed from blacklist by pattern: " + entry_id);
            
            // 自动保存
            if (!storage_file_.empty()) {
                lock.unlock();
                save();
            }
            return true;
        }
    }
    
    LOG_WARNING("Blacklist entry not found: " + entry_id);
    return false;
}

void IPBlacklistManager::removeEntryInternal(const BlacklistEntry& entry) {
    // 从pattern_to_id_中移除
    pattern_to_id_.erase(entry.ip_pattern);
    
    // 根据类型从相应数据结构中移除
    switch (entry.type) {
        case BlacklistEntryType::SINGLE_IP_V4: {
            uint32_t ip_int = IPUtils::ipToUint32(entry.ip_pattern);
            ipv4_single_ips_.erase(ip_int);
            break;
        }
        
        case BlacklistEntryType::SINGLE_IP_V6: {
            ipv6_single_ips_.erase(entry.ip_pattern);
            break;
        }
        
        case BlacklistEntryType::RANGE_IP_V4:
        case BlacklistEntryType::CIDR_IP_V4: {
            // 从范围列表中移除
            ipv4_ranges_.erase(
                std::remove_if(ipv4_ranges_.begin(), ipv4_ranges_.end(),
                    [&entry](const IPv4Range& range) {
                        return range.entry_id == entry.id;
                    }),
                ipv4_ranges_.end()
            );
            break;
        }
        
        default:
            break;
    }
}

bool IPBlacklistManager::isBlacklisted(const std::string& ip) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    // 判断是IPv4还是IPv6
    if (IPUtils::isValidIPv4(ip)) {
        uint32_t ip_int = IPUtils::ipToUint32(ip);
        return isIPv4Blacklisted(ip_int);
    } else if (IPUtils::isValidIPv6(ip)) {
        return isIPv6Blacklisted(ip);
    }
    
    return false;
}

bool IPBlacklistManager::isIPv4Blacklisted(uint32_t ip) const {
    auto now = std::chrono::system_clock::now();  // ⭐⭐⭐ 新增：获取当前时间
    
    // 检查单IP黑名单
    if (ipv4_single_ips_.count(ip) > 0) {
        // ⭐⭐⭐ 新增：需要查找对应的entry检查是否过期
        std::string ip_str = IPUtils::uint32ToIp(ip);
        auto pattern_it = pattern_to_id_.find(ip_str);
        if (pattern_it != pattern_to_id_.end()) {
            auto entry_it = entries_.find(pattern_it->second);
            if (entry_it != entries_.end()) {
                const auto& entry = entry_it->second;
                // 检查是否过期
                if (!entry.is_permanent && entry.expires_at <= now) {
                    return false;  // 已过期，不在黑名单中
                }
                return true;  // 未过期，仍在黑名单中
            }
        }
        return true;  // 找不到entry信息，保守处理：仍认为在黑名单中
    }
    
    // 检查范围黑名单
    for (const auto& range : ipv4_ranges_) {
        if (range.contains(ip)) {
            // ⭐⭐⭐ 新增：检查该范围对应的entry是否过期
            auto entry_it = entries_.find(range.entry_id);
            if (entry_it != entries_.end()) {
                const auto& entry = entry_it->second;
                // 检查是否过期
                if (!entry.is_permanent && entry.expires_at <= now) {
                    continue;  // 已过期，继续检查下一个范围
                }
                return true;  // 未过期，仍在黑名单中
            }
            return true;  // 找不到entry信息，保守处理
        }
    }
    
    return false;
}

bool IPBlacklistManager::isIPv6Blacklisted(const std::string& ip) const {
    auto now = std::chrono::system_clock::now();  // ⭐⭐⭐ 新增：获取当前时间
    
    // 简化实现：仅检查单IP黑名单
    if (ipv6_single_ips_.count(ip) > 0) {
        // ⭐⭐⭐ 新增：检查是否过期
        auto pattern_it = pattern_to_id_.find(ip);
        if (pattern_it != pattern_to_id_.end()) {
            auto entry_it = entries_.find(pattern_it->second);
            if (entry_it != entries_.end()) {
                const auto& entry = entry_it->second;
                // 检查是否过期
                if (!entry.is_permanent && entry.expires_at <= now) {
                    return false;  // 已过期，不在黑名单中
                }
                return true;  // 未过期，仍在黑名单中
            }
        }
        return true;  // 找不到entry信息，保守处理
    }
    
    return false;
}

void IPBlacklistManager::addIPv4Single(uint32_t ip, const std::string& /* entry_id */) {
    ipv4_single_ips_.insert(ip);
}

void IPBlacklistManager::addIPv6Single(const std::string& ip, const std::string& /* entry_id */) {
    ipv6_single_ips_.insert(ip);
}

void IPBlacklistManager::addIPv4Range(uint32_t start, uint32_t end, const std::string& entry_id) {
    IPv4Range range;
    range.start = start;
    range.end = end;
    range.entry_id = entry_id;
    ipv4_ranges_.push_back(range);
}

std::vector<BlacklistEntry> IPBlacklistManager::getAllEntries() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<BlacklistEntry> entries;
    entries.reserve(entries_.size());
    
    for (const auto& [id, entry] : entries_) {
        entries.push_back(entry);
    }
    
    return entries;
}

nlohmann::json IPBlacklistManager::getStatistics() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    nlohmann::json stats;
    stats["total_entries"] = entries_.size();
    stats["ipv4_single_count"] = ipv4_single_ips_.size();
    stats["ipv6_single_count"] = ipv6_single_ips_.size();
    stats["ipv4_range_count"] = ipv4_ranges_.size();
    
    // 统计永久和临时黑名单
    int permanent_count = 0;
    int temporary_count = 0;
    for (const auto& [id, entry] : entries_) {
        if (entry.is_permanent) {
            permanent_count++;
        } else {
            temporary_count++;
        }
    }
    stats["permanent_count"] = permanent_count;
    stats["temporary_count"] = temporary_count;
    
    return stats;
}

void IPBlacklistManager::clearAll() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    ipv4_single_ips_.clear();
    ipv6_single_ips_.clear();
    ipv4_ranges_.clear();
    entries_.clear();
    pattern_to_id_.clear();
    
    LOG_INFO("Cleared all blacklist entries");
    
    // 自动保存
    if (!storage_file_.empty()) {
        lock.unlock();
        save();
    }
}

size_t IPBlacklistManager::cleanupExpiredEntries() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    std::vector<std::string> expired_ids;
    
    // 查找过期条目
    for (const auto& [id, entry] : entries_) {
        if (!entry.is_permanent && entry.expires_at <= now) {
            expired_ids.push_back(id);
        }
    }
    
    // 移除过期条目
    for (const auto& id : expired_ids) {
        auto it = entries_.find(id);
        if (it != entries_.end()) {
            removeEntryInternal(it->second);
            entries_.erase(it);
        }
    }
    
    if (!expired_ids.empty()) {
        LOG_INFO("Cleaned up " + std::to_string(expired_ids.size()) + " expired blacklist entries");
        
        // 自动保存
        if (!storage_file_.empty()) {
            lock.unlock();
            save();
        }
    }
    
    return expired_ids.size();
}

bool IPBlacklistManager::save() {
    if (storage_file_.empty()) {
        return false;
    }
    
    try {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        
        nlohmann::json j;
        j["version"] = "1.0";
        j["entries"] = nlohmann::json::array();
        
        for (const auto& [id, entry] : entries_) {
            j["entries"].push_back(entry.toJson());
        }
        
        lock.unlock();
        
        // 写入文件
        std::ofstream ofs(storage_file_);
        if (!ofs.is_open()) {
            LOG_ERROR("Failed to open blacklist file for writing: " + storage_file_);
            return false;
        }
        
        ofs << j.dump(2);
        ofs.close();
        
        LOG_DEBUG("Saved blacklist to file: " + storage_file_);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to save blacklist: " + std::string(e.what()));
        return false;
    }
}

bool IPBlacklistManager::load() {
    if (storage_file_.empty()) {
        return false;
    }
    
    try {
        std::ifstream ifs(storage_file_);
        if (!ifs.is_open()) {
            LOG_WARNING("Blacklist file not found, starting with empty blacklist: " + storage_file_);
            return false;
        }
        
        nlohmann::json j;
        ifs >> j;
        ifs.close();
        
        std::unique_lock<std::shared_mutex> lock(mutex_);
        
        // 清空现有数据
        ipv4_single_ips_.clear();
        ipv6_single_ips_.clear();
        ipv4_ranges_.clear();
        entries_.clear();
        pattern_to_id_.clear();
        
        // 加载条目
        if (j.contains("entries") && j["entries"].is_array()) {
            for (const auto& entry_json : j["entries"]) {
                BlacklistEntry entry = BlacklistEntry::fromJson(entry_json);
                
                // 根据类型重建数据结构
                switch (entry.type) {
                    case BlacklistEntryType::SINGLE_IP_V4: {
                        uint32_t ip_int = IPUtils::ipToUint32(entry.ip_pattern);
                        addIPv4Single(ip_int, entry.id);
                        break;
                    }
                    
                    case BlacklistEntryType::SINGLE_IP_V6: {
                        addIPv6Single(entry.ip_pattern, entry.id);
                        break;
                    }
                    
                    case BlacklistEntryType::RANGE_IP_V4:
                    case BlacklistEntryType::CIDR_IP_V4: {
                        uint32_t start_int = IPUtils::ipToUint32(entry.ip_range_start);
                        uint32_t end_int = IPUtils::ipToUint32(entry.ip_range_end);
                        addIPv4Range(start_int, end_int, entry.id);
                        break;
                    }
                    
                    default:
                        break;
                }
                
                entries_[entry.id] = entry;
                pattern_to_id_[entry.ip_pattern] = entry.id;
            }
        }
        
        LOG_INFO("Loaded " + std::to_string(entries_.size()) + " blacklist entries from file");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load blacklist: " + std::string(e.what()));
        return false;
    }
}

void IPBlacklistManager::enableAutoCleanup(common::network::EventLoop* event_loop, int interval_seconds) {
    if (auto_cleanup_enabled_.load()) {
        LOG_WARNING("Auto cleanup is already enabled");
        return;
    }
    
    if (!event_loop) {
        LOG_ERROR("EventLoop pointer is null, cannot enable auto cleanup");
        return;
    }
    
    event_loop_ = event_loop;
    auto_cleanup_enabled_.store(true);
    
    // ⭐⭐⭐ 使用EventLoop的runEvery来注册定时任务（而不是创建线程）
    cleanup_timer_id_ = event_loop_->runEvery(
        interval_seconds * 1000,  // 转换为毫秒
        [this]() {
            size_t cleaned = cleanupExpiredEntries();
            if (cleaned > 0) {
                LOG_INFO("✨ Auto cleanup removed " + std::to_string(cleaned) + " expired blacklist entries");
            }
        }
    );
    
    LOG_INFO("✅ Enabled auto cleanup with interval: " + std::to_string(interval_seconds) + " seconds (using EventLoop timer)");
    LOG_INFO("   - Timer ID: " + std::to_string(cleanup_timer_id_));
}

void IPBlacklistManager::disableAutoCleanup() {
    if (!auto_cleanup_enabled_.load()) {
        return;
    }
    
    auto_cleanup_enabled_.store(false);
    
    // ⭐⭐⭐ 取消EventLoop中的定时任务
    if (event_loop_ && cleanup_timer_id_ != 0) {
        event_loop_->cancelTimer(cleanup_timer_id_);
        LOG_INFO("✅ Cancelled cleanup timer (ID: " + std::to_string(cleanup_timer_id_) + ")");
        cleanup_timer_id_ = 0;
    }
    
    event_loop_ = nullptr;
    LOG_INFO("❌ Disabled auto cleanup");
}

} // namespace utils
} // namespace common
