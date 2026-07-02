/**
 * @file crypto.h
 * @brief 加密解密工具统一接口
 * @author AI Assistant
 * @date 2025-11-06
 * 
 * 提供统一的加密解密工具接口，包含：
 * - 哈希算法：MD5、SHA1、SHA256、SHA512
 * - 编码算法：Base64、Base64URL、Hex
 * - 对称加密：AES-256-CBC、AES-256-GCM
 * - HMAC签名：HMAC-SHA256、HMAC-SHA512
 * - 随机数生成：安全随机字符串、UUID
 */

#ifndef COMMON_UTILS_CRYPTO_H
#define COMMON_UTILS_CRYPTO_H

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace common {
namespace utils {

/**
 * @brief 加密解密工具类
 * 
 * 统一的加密解密工具接口，基于OpenSSL实现
 */
class Crypto {
public:
    // =====================================================
    // 1. 哈希算法（Hash Algorithms）
    // =====================================================
    
    /**
     * @brief 计算MD5哈希值
     * @param input 输入字符串
     * @return MD5哈希值（32位十六进制小写字符串）
     */
    static std::string md5(const std::string& input);
    
    /**
     * @brief 计算SHA1哈希值
     * @param input 输入字符串
     * @return SHA1哈希值（40位十六进制小写字符串）
     */
    static std::string sha1(const std::string& input);
    
    /**
     * @brief 计算SHA256哈希值
     * @param input 输入字符串
     * @return SHA256哈希值（64位十六进制小写字符串）
     */
    static std::string sha256(const std::string& input);
    
    /**
     * @brief 计算SHA512哈希值
     * @param input 输入字符串
     * @return SHA512哈希值（128位十六进制小写字符串）
     */
    static std::string sha512(const std::string& input);
    
    // =====================================================
    // 2. 编码算法（Encoding Algorithms）
    // =====================================================
    
    /**
     * @brief Base64编码
     * @param input 输入字符串
     * @return Base64编码后的字符串
     */
    static std::string base64Encode(const std::string& input);
    
    /**
     * @brief Base64解码
     * @param input Base64编码的字符串
     * @return 解码后的字符串
     */
    static std::string base64Decode(const std::string& input);
    
    /**
     * @brief Base64URL编码（JWT标准）
     * @param input 输入字符串
     * @return Base64URL编码后的字符串（无=填充，+→-，/→_）
     */
    static std::string base64UrlEncode(const std::string& input);
    
    /**
     * @brief Base64URL解码（JWT标准）
     * @param input Base64URL编码的字符串
     * @return 解码后的字符串
     */
    static std::string base64UrlDecode(const std::string& input);
    
    /**
     * @brief 十六进制编码
     * @param input 输入字符串
     * @param uppercase 是否使用大写字母（默认小写）
     * @return 十六进制编码后的字符串
     */
    static std::string hexEncode(const std::string& input, bool uppercase = false);
    
    /**
     * @brief 十六进制解码
     * @param input 十六进制编码的字符串
     * @return 解码后的字符串
     */
    static std::string hexDecode(const std::string& input);
    
    // =====================================================
    // 3. 对称加密算法（Symmetric Encryption）
    // =====================================================
    
    /**
     * @brief AES-256-CBC 加密
     * @param plaintext 明文
     * @param key 密钥（32字节）
     * @param iv 初始化向量（16字节）
     * @return 密文（Base64编码）
     */
    static std::string aes256CbcEncrypt(
        const std::string& plaintext,
        const std::string& key,
        const std::string& iv
    );
    
    /**
     * @brief AES-256-CBC 解密
     * @param ciphertext 密文（Base64编码）
     * @param key 密钥（32字节）
     * @param iv 初始化向量（16字节）
     * @return 明文
     */
    static std::string aes256CbcDecrypt(
        const std::string& ciphertext,
        const std::string& key,
        const std::string& iv
    );
    
    /**
     * @brief AES-256-GCM 加密（带认证）
     * @param plaintext 明文
     * @param key 密钥（32字节）
     * @param iv 初始化向量（12字节，推荐）
     * @param aad 附加认证数据（可选）
     * @param tag 输出：认证标签（16字节）
     * @return 密文（Base64编码）
     */
    static std::string aes256GcmEncrypt(
        const std::string& plaintext,
        const std::string& key,
        const std::string& iv,
        const std::string& aad,
        std::string& tag
    );
    
    /**
     * @brief AES-256-GCM 解密（带认证）
     * @param ciphertext 密文（Base64编码）
     * @param key 密钥（32字节）
     * @param iv 初始化向量（12字节）
     * @param aad 附加认证数据
     * @param tag 认证标签（16字节）
     * @return 明文
     */
    static std::string aes256GcmDecrypt(
        const std::string& ciphertext,
        const std::string& key,
        const std::string& iv,
        const std::string& aad,
        const std::string& tag
    );
    
    // =====================================================
    // 4. HMAC签名（HMAC Signature）
    // =====================================================
    
    /**
     * @brief HMAC-SHA256 签名
     * @param message 消息
     * @param key 密钥
     * @return HMAC签名（十六进制字符串）
     */
    static std::string hmacSha256(const std::string& message, const std::string& key);
    
    /**
     * @brief HMAC-SHA256 签名（原始字节）
     * @param message 消息
     * @param key 密钥
     * @return HMAC签名（原始字节）
     */
    static std::string hmacSha256Raw(const std::string& message, const std::string& key);
    
    /**
     * @brief HMAC-SHA512 签名
     * @param message 消息
     * @param key 密钥
     * @return HMAC签名（十六进制字符串）
     */
    static std::string hmacSha512(const std::string& message, const std::string& key);
    
    // =====================================================
    // 5. 随机数生成（Random Generation）
    // =====================================================
    
    /**
     * @brief 生成随机字节
     * @param length 字节数
     * @return 随机字节数组
     */
    static std::vector<uint8_t> randomBytes(size_t length);
    
    /**
     * @brief 生成随机字符串（字母+数字）
     * @param length 字符串长度
     * @return 随机字符串
     */
    static std::string randomString(size_t length);
    
    /**
     * @brief 生成随机十六进制字符串
     * @param length 字符串长度
     * @return 随机十六进制字符串
     */
    static std::string randomHex(size_t length);
    
    /**
     * @brief 生成密码盐值
     * @param length 盐值长度（默认16字节）
     * @return 盐值（Base64编码）
     */
    static std::string generateSalt(size_t length = 16);
    
    /**
     * @brief 生成UUID v4
     * @return UUID字符串（如：550e8400-e29b-41d4-a716-446655440000）
     */
    static std::string generateUuid();
    
    // =====================================================
    // 6. 密钥派生（Key Derivation）
    // =====================================================
    
    /**
     * @brief PBKDF2密钥派生（基于HMAC-SHA256）
     * @param password 密码
     * @param salt 盐值
     * @param iterations 迭代次数（推荐100000+）
     * @param key_length 输出密钥长度（字节）
     * @return 派生密钥（十六进制字符串）
     */
    static std::string pbkdf2Sha256(
        const std::string& password,
        const std::string& salt,
        int iterations,
        size_t key_length
    );
    
    // =====================================================
    // 7. 工具方法（Utility Methods）
    // =====================================================
    
    /**
     * @brief 检查字符串是否为有效的Base64
     * @param input 输入字符串
     * @return 是否有效
     */
    static bool isValidBase64(const std::string& input);
    
    /**
     * @brief 检查字符串是否为有效的十六进制
     * @param input 输入字符串
     * @return 是否有效
     */
    static bool isValidHex(const std::string& input);
};

} // namespace utils
} // namespace common

#endif // COMMON_UTILS_CRYPTO_H
