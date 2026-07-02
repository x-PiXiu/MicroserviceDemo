#ifndef COMMON_UTILS_MD5_H
#define COMMON_UTILS_MD5_H

#include <string>
#include <cstdint>

namespace common {
namespace utils {

/**
 * @brief MD5哈希工具类
 * 
 * 提供MD5哈希计算功能，用于密码加密等场景
 */
class MD5 {
public:
    /**
     * @brief 计算字符串的MD5哈希值
     * @param input 输入字符串
     * @return MD5哈希值（32位十六进制字符串）
     */
    static std::string encode(const std::string& input);
    
    /**
     * @brief 计算二进制数据的MD5哈希值
     * @param data 输入数据
     * @param length 数据长度
     * @return MD5哈希值（32位十六进制字符串）
     */
    static std::string encode(const unsigned char* data, size_t length);

private:
    // MD5上下文结构
    struct MD5Context {
        uint32_t state[4];
        uint32_t count[2];
        uint8_t buffer[64];
    };
    
    static void init(MD5Context* context);
    static void update(MD5Context* context, const uint8_t* input, size_t length);
    static void final(uint8_t digest[16], MD5Context* context);
    static void transform(uint32_t state[4], const uint8_t block[64]);
    
    // 辅助函数
    static inline uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
    static inline uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
    static inline uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
    static inline uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }
    static inline uint32_t rotateLeft(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
    
    static void FF(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac);
    static void GG(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac);
    static void HH(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac);
    static void II(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac);
};

} // namespace utils
} // namespace common

#endif // COMMON_UTILS_MD5_H
