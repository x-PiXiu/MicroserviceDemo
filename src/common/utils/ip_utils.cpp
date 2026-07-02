#include "common/utils/ip_utils.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <vector>

namespace common {
namespace utils {

// 去除字符串首尾空白
std::string IPUtils::trim(const std::string& str) {
    if (str.empty()) {
        return str;
    }
    
    size_t start = 0;
    size_t end = str.length();
    
    // 查找第一个非空白字符
    while (start < end && std::isspace(static_cast<unsigned char>(str[start]))) {
        ++start;
    }
    
    // 查找最后一个非空白字符
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        --end;
    }
    
    return str.substr(start, end - start);
}

// 验证IPv4地址是否有效
bool IPUtils::isValidIPv4(const std::string& ip) {
    if (ip.empty()) {
        return false;
    }
    
    std::istringstream iss(ip);
    std::string octet;
    int count = 0;
    
    while (std::getline(iss, octet, '.')) {
        // 检查是否为空
        if (octet.empty()) {
            return false;
        }
        
        // 检查是否全为数字
        for (char c : octet) {
            if (!std::isdigit(c)) {
                return false;
            }
        }
        
        // 转换为整数
        try {
            int value = std::stoi(octet);
            if (value < 0 || value > 255) {
                return false;
            }
        } catch (...) {
            return false;
        }
        
        ++count;
    }
    
    // IPv4必须有4个部分
    return count == 4;
}

// ⭐ 新墟：验证IPv6地址是否有效
bool IPUtils::isValidIPv6(const std::string& ip) {
    if (ip.empty()) {
        return false;
    }
    
    // IPv6基本格式验证：包含冒号，只包含0-9a-fA-F和冒号
    // 支持的格式例子：
    // - 2001:0db8:85a3:0000:0000:8a2e:0370:7334
    // - 2001:db8:85a3::8a2e:370:7334 (省略连续的0)
    // - ::1 (本地环回)
    // - fe80::1 (链路本地)
    
    if (ip.find(':') == std::string::npos) {
        return false;
    }
    
    // 简单验证：检查字符是否合法
    int colon_count = 0;
    int double_colon_count = 0;
    bool has_double_colon = false;
    
    for (size_t i = 0; i < ip.length(); ++i) {
        char c = ip[i];
        
        if (c == ':') {
            colon_count++;
            if (i + 1 < ip.length() && ip[i + 1] == ':') {
                if (has_double_colon) {
                    return false;  // 只能有一个::
                }
                has_double_colon = true;
                double_colon_count++;
                i++;  // 跳过下一个冒号
            }
        } else if (!std::isxdigit(c)) {
            return false;  // 不是16进制字符
        }
    }
    
    // IPv6基本验证：冒号数量应在合理范围内
    if (has_double_colon) {
        // 如果有::，冒号数量应该小于7
        if (colon_count > 7) {
            return false;
        }
    } else {
        // 如果没有::，应该正好有7个冒号（8个组）
        if (colon_count != 7) {
            return false;
        }
    }
    
    return true;
}

// IP字符串转uint32
uint32_t IPUtils::ipToUint32(const std::string& ip) {
    if (!isValidIPv4(ip)) {
        throw std::invalid_argument("无效的IP地址: " + ip);
    }
    
    uint32_t result = 0;
    std::istringstream iss(ip);
    std::string octet;
    int shift = 24;
    
    while (std::getline(iss, octet, '.')) {
        uint32_t value = std::stoul(octet);
        result |= (value << shift);
        shift -= 8;
    }
    
    return result;
}

// uint32转IP字符串
std::string IPUtils::uint32ToIp(uint32_t ip) {
    std::ostringstream oss;
    oss << ((ip >> 24) & 0xFF) << "."
        << ((ip >> 16) & 0xFF) << "."
        << ((ip >> 8) & 0xFF) << "."
        << (ip & 0xFF);
    return oss.str();
}

// 解析CIDR格式
std::pair<std::string, std::string> IPUtils::parseCIDR(const std::string& cidr) {
    size_t pos = cidr.find('/');
    if (pos == std::string::npos) {
        throw std::invalid_argument("无效的CIDR格式（缺少'/'）: " + cidr);
    }
    
    std::string network_ip = trim(cidr.substr(0, pos));
    std::string prefix_str = trim(cidr.substr(pos + 1));
    
    // 验证网络IP
    if (!isValidIPv4(network_ip)) {
        throw std::invalid_argument("无效的CIDR网络地址: " + network_ip);
    }
    
    // 解析前缀长度
    int prefix_len;
    try {
        prefix_len = std::stoi(prefix_str);
    } catch (...) {
        throw std::invalid_argument("无效的CIDR前缀长度: " + prefix_str);
    }
    
    // IPv4前缀长度验证（0-32）
    if (prefix_len < 0 || prefix_len > 32) {
        throw std::invalid_argument("CIDR前缀长度必须在0-32之间: " + std::to_string(prefix_len));
    }
    
    // 将IP转换为32位整数
    uint32_t ip_int = ipToUint32(network_ip);
    
    // 计算子网掩码
    uint32_t mask = (prefix_len == 0) ? 0 : (0xFFFFFFFF << (32 - prefix_len));
    
    // 计算网络地址（起始IP）
    uint32_t network_addr = ip_int & mask;
    
    // 计算广播地址（结束IP）
    uint32_t broadcast_addr = network_addr | (~mask);
    
    // 转换回IP字符串
    std::string start_ip = uint32ToIp(network_addr);
    std::string end_ip = uint32ToIp(broadcast_addr);
    
    return {start_ip, end_ip};
}

// 计算IP段包含的IP数量
uint32_t IPUtils::calculateIPCount(const std::string& range_start, 
                                   const std::string& range_end) {
    uint32_t start_int = ipToUint32(range_start);
    uint32_t end_int = ipToUint32(range_end);
    
    if (end_int < start_int) {
        throw std::invalid_argument("结束IP不能小于起始IP");
    }
    
    // 注意：这里需要用uint64_t防止溢出
    uint64_t count = static_cast<uint64_t>(end_int) - static_cast<uint64_t>(start_int) + 1;
    
    return static_cast<uint32_t>(count);
}

// 检查IP是否在范围内
bool IPUtils::isIPInRange(const std::string& ip, 
                         const std::string& range_start, 
                         const std::string& range_end) {
    try {
        uint32_t ip_int = ipToUint32(ip);
        uint32_t start_int = ipToUint32(range_start);
        uint32_t end_int = ipToUint32(range_end);
        
        return ip_int >= start_int && ip_int <= end_int;
    } catch (...) {
        return false;
    }
}

// 解析IP地址（主函数）
IPWhitelistInfo IPUtils::parseIPAddress(const std::string& ip_input) {
    IPWhitelistInfo info;
    std::string trimmed_input = trim(ip_input);
    
    if (trimmed_input.empty()) {
        throw std::invalid_argument("IP地址不能为空");
    }
    
    info.ip_address = trimmed_input;
    
    // ⭐ 新墟：优先检查是否为IPv6地址
    if (trimmed_input.find(':') != std::string::npos && 
        trimmed_input.find('/') == std::string::npos && 
        trimmed_input.find('-') == std::string::npos) {
        
        // 可能是IPv6地址，进行验证
        if (isValidIPv6(trimmed_input)) {
            info.type = IPWhitelistType::SINGLE_IP;
            info.is_range = false;
            info.ip_range_start = "";
            info.ip_range_end = "";
            info.ip_count = 1;
            return info;
        }
    }
    
    // 1. 检查是否为CIDR格式：192.168.1.0/24
    if (trimmed_input.find('/') != std::string::npos) {
        info.type = IPWhitelistType::CIDR_IP;
        info.is_range = true;
        
        try {
            // 解析CIDR
            auto cidr_pair = parseCIDR(trimmed_input);
            info.ip_range_start = cidr_pair.first;
            info.ip_range_end = cidr_pair.second;
            
            // 计算IP数量
            info.ip_count = calculateIPCount(info.ip_range_start, info.ip_range_end);
            
            // 检查IP段大小限制
            if (info.ip_count > MAX_IP_RANGE_SIZE) {
                throw std::invalid_argument(
                    "IP段过大（" + std::to_string(info.ip_count) + "个IP），" +
                    "最大支持" + std::to_string(MAX_IP_RANGE_SIZE) + "个IP，" +
                    "建议拆分为多个较小的IP段"
                );
            }
            
        } catch (const std::exception& e) {
            throw std::invalid_argument("CIDR格式解析失败: " + std::string(e.what()));
        }
        
        return info;
    }
    
    // 2. 检查是否为范围格式：192.168.1.1-192.168.1.254
    if (trimmed_input.find('-') != std::string::npos) {
        info.type = IPWhitelistType::RANGE_IP;
        info.is_range = true;
        
        size_t pos = trimmed_input.find('-');
        info.ip_range_start = trim(trimmed_input.substr(0, pos));
        info.ip_range_end = trim(trimmed_input.substr(pos + 1));
        
        // 验证IP有效性
        if (!isValidIPv4(info.ip_range_start)) {
            throw std::invalid_argument("无效的起始IP地址: " + info.ip_range_start);
        }
        
        if (!isValidIPv4(info.ip_range_end)) {
            throw std::invalid_argument("无效的结束IP地址: " + info.ip_range_end);
        }
        
        // 验证范围顺序
        uint32_t start_int = ipToUint32(info.ip_range_start);
        uint32_t end_int = ipToUint32(info.ip_range_end);
        
        if (end_int < start_int) {
            throw std::invalid_argument(
                "结束IP(" + info.ip_range_end + ")不能小于起始IP(" + info.ip_range_start + ")"
            );
        }
        
        // 计算IP数量
        info.ip_count = calculateIPCount(info.ip_range_start, info.ip_range_end);
        
        // 检查IP段大小限制
        if (info.ip_count > MAX_IP_RANGE_SIZE) {
            throw std::invalid_argument(
                "IP段过大（" + std::to_string(info.ip_count) + "个IP），" +
                "最大支持" + std::to_string(MAX_IP_RANGE_SIZE) + "个IP，" +
                "建议拆分为多个较小的IP段"
            );
        }
        
        return info;
    }
    
    // 3. 单IP（IPv4或IPv6）
    info.type = IPWhitelistType::SINGLE_IP;
    info.is_range = false;
    info.ip_range_start = "";
    info.ip_range_end = "";
    info.ip_count = 1;
    
    // ⭐ 验证IP有效性（支持IPv4和IPv6）
    if (!isValidIPv4(trimmed_input) && !isValidIPv6(trimmed_input)) {
        throw std::invalid_argument("无效的IP地址格式(支持IPv4和IPv6): " + trimmed_input);
    }
    
    return info;
}

} // namespace utils
} // namespace common
