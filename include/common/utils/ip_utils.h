#pragma once

#include <string>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace common {
namespace utils {

/**
 * @brief IP白名单类型枚举
 */
enum class IPWhitelistType {
    SINGLE_IP,      // 单IP：192.168.1.100
    RANGE_IP,       // 范围IP：192.168.1.1-192.168.1.254
    CIDR_IP         // CIDR：192.168.1.0/24
};

/**
 * @brief IP白名单信息结构
 */
struct IPWhitelistInfo {
    IPWhitelistType type;           // IP类型
    std::string ip_address;         // 原始输入
    std::string ip_range_start;     // 起始IP（IP段时有效）
    std::string ip_range_end;       // 结束IP（IP段时有效）
    bool is_range;                  // 是否IP段
    uint32_t ip_count;              // IP数量（单IP时为1，IP段时为范围大小）
    
    IPWhitelistInfo() : type(IPWhitelistType::SINGLE_IP), is_range(false), ip_count(1) {}
};

/**
 * @brief IP地址工具类
 * 
 * 功能：
 * - 解析IP格式（单IP、IP段、CIDR）
 * - IP有效性验证
 * - IP地址转换（字符串<->uint32）
 * - IP范围匹配
 */
class IPUtils {
public:
    /**
     * @brief 解析IP地址，自动识别格式
     * 
     * 支持格式：
     * - 单IP：192.168.1.100
     * - IP范围：192.168.1.1-192.168.1.254
     * - CIDR：192.168.1.0/24
     * 
     * @param ip_input 输入的IP字符串
     * @return IPWhitelistInfo IP白名单信息
     * @throw std::invalid_argument IP格式无效
     */
    static IPWhitelistInfo parseIPAddress(const std::string& ip_input);
    
    /**
     * @brief 解析CIDR格式，返回起始IP和结束IP
     * 
     * @param cidr CIDR字符串（如：192.168.1.0/24）
     * @return std::pair<std::string, std::string> (起始IP, 结束IP)
     * @throw std::invalid_argument CIDR格式无效
     */
    static std::pair<std::string, std::string> parseCIDR(const std::string& cidr);
    
    /**
     * @brief 验证IP地址是否有效（IPv4）
     * 
     * @param ip IP地址字符串
     * @return true 有效
     * @return false 无效
     */
    static bool isValidIPv4(const std::string& ip);
    
    /**
     * @brief ⭐ 验证IP地址是否有效（IPv6）
     * 
     * @param ip IP地址字符串
     * @return true 有效
     * @return false 无效
     */
    static bool isValidIPv6(const std::string& ip);
    
    /**
     * @brief IP字符串转uint32（IPv4）
     * 
     * @param ip IP地址字符串（如：192.168.1.1）
     * @return uint32_t IP的32位整数表示
     * @throw std::invalid_argument IP格式无效
     */
    static uint32_t ipToUint32(const std::string& ip);
    
    /**
     * @brief uint32转IP字符串（IPv4）
     * 
     * @param ip 32位整数表示的IP
     * @return std::string IP地址字符串
     */
    static std::string uint32ToIp(uint32_t ip);
    
    /**
     * @brief 检查IP是否在范围内
     * 
     * @param ip 待检查的IP
     * @param range_start 范围起始IP
     * @param range_end 范围结束IP
     * @return true 在范围内
     * @return false 不在范围内
     */
    static bool isIPInRange(const std::string& ip, 
                           const std::string& range_start, 
                           const std::string& range_end);
    
    /**
     * @brief 计算IP段包含的IP数量
     * 
     * @param range_start 起始IP
     * @param range_end 结束IP
     * @return uint32_t IP数量
     */
    static uint32_t calculateIPCount(const std::string& range_start, 
                                     const std::string& range_end);
    
    /**
     * @brief 去除字符串首尾空白
     * 
     * @param str 输入字符串
     * @return std::string 处理后的字符串
     */
    static std::string trim(const std::string& str);
    
private:
    // 最大IP段大小限制（防止过大的IP段）
    static constexpr uint32_t MAX_IP_RANGE_SIZE = 65536;  // 2^16，最大支持/16网段
};

} // namespace utils
} // namespace common
