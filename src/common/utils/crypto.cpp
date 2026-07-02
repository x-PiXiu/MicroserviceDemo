/**
 * @file crypto.cpp
 * @brief 加密解密工具实现
 * @author AI Assistant
 * @date 2025-11-06
 */

#include "common/utils/crypto.h"
#include "common/utils/md5.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/aes.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <stdexcept>
#include <cstring>

namespace common {
namespace utils {

// =====================================================
// 1. 哈希算法实现
// =====================================================

std::string Crypto::md5(const std::string& input) {
    // 复用已有的MD5实现
    return MD5::encode(input);
}

std::string Crypto::sha1(const std::string& input) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);
    
    std::ostringstream oss;
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string Crypto::sha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);
    
    std::ostringstream oss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string Crypto::sha512(const std::string& input) {
    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);
    
    std::ostringstream oss;
    for (int i = 0; i < SHA512_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// =====================================================
// 2. 编码算法实现
// =====================================================

std::string Crypto::base64Encode(const std::string& input) {
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string result;
    int val = 0;
    int valb = -6;
    
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    
    if (valb > -6) {
        result.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    
    while (result.size() % 4) {
        result.push_back('=');
    }
    
    return result;
}

std::string Crypto::base64Decode(const std::string& input) {
    static const unsigned char base64_decode_table[256] = {
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,62,64,64,64,63,
        52,53,54,55,56,57,58,59,60,61,64,64,64,64,64,64,
        64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,64,64,64,64,64,
        64,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
        64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64
    };
    
    std::string result;
    int val = 0;
    int valb = -8;
    
    for (unsigned char c : input) {
        if (base64_decode_table[c] == 64) break;
        val = (val << 6) + base64_decode_table[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return result;
}

std::string Crypto::base64UrlEncode(const std::string& input) {
    std::string result = base64Encode(input);
    
    // 转换为URL安全格式（JWT标准）
    for (char& c : result) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    
    // 移除末尾的=填充
    while (!result.empty() && result.back() == '=') {
        result.pop_back();
    }
    
    return result;
}

std::string Crypto::base64UrlDecode(const std::string& input) {
    std::string result = input;
    
    // 还原标准Base64字符
    for (char& c : result) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    
    // 补充=填充（Base64要求长度是4的倍数）
    while (result.length() % 4 != 0) {
        result.push_back('=');
    }
    
    return base64Decode(result);
}

std::string Crypto::hexEncode(const std::string& input, bool uppercase) {
    std::ostringstream oss;
    if (uppercase) {
        oss << std::hex << std::uppercase;
    } else {
        oss << std::hex;
    }
    
    for (unsigned char c : input) {
        oss << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    
    return oss.str();
}

std::string Crypto::hexDecode(const std::string& input) {
    if (input.length() % 2 != 0) {
        throw std::invalid_argument("Invalid hex string length");
    }
    
    std::string result;
    result.reserve(input.length() / 2);
    
    for (size_t i = 0; i < input.length(); i += 2) {
        std::string byte_str = input.substr(i, 2);
        char byte = static_cast<char>(std::stoi(byte_str, nullptr, 16));
        result.push_back(byte);
    }
    
    return result;
}

// =====================================================
// 3. 对称加密算法实现
// =====================================================

std::string Crypto::aes256CbcEncrypt(
    const std::string& plaintext,
    const std::string& key,
    const std::string& iv
) {
    if (key.length() != 32) {
        throw std::invalid_argument("AES-256 requires 32-byte key");
    }
    if (iv.length() != 16) {
        throw std::invalid_argument("AES-CBC requires 16-byte IV");
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }
    
    // 初始化加密操作
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.c_str()),
                           reinterpret_cast<const unsigned char*>(iv.c_str())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize encryption");
    }
    
    // 分配输出缓冲区（明文长度 + 块大小）
    std::vector<unsigned char> ciphertext(plaintext.length() + AES_BLOCK_SIZE);
    int len = 0;
    int ciphertext_len = 0;
    
    // 加密数据
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                          reinterpret_cast<const unsigned char*>(plaintext.c_str()),
                          plaintext.length()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryption failed");
    }
    ciphertext_len = len;
    
    // 完成加密（处理填充）
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryption finalization failed");
    }
    ciphertext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    
    // 返回Base64编码的密文
    return base64Encode(std::string(ciphertext.begin(), ciphertext.begin() + ciphertext_len));
}

std::string Crypto::aes256CbcDecrypt(
    const std::string& ciphertext,
    const std::string& key,
    const std::string& iv
) {
    if (key.length() != 32) {
        throw std::invalid_argument("AES-256 requires 32-byte key");
    }
    if (iv.length() != 16) {
        throw std::invalid_argument("AES-CBC requires 16-byte IV");
    }
    
    // Base64解码密文
    std::string decoded_ciphertext = base64Decode(ciphertext);
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }
    
    // 初始化解密操作
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                           reinterpret_cast<const unsigned char*>(key.c_str()),
                           reinterpret_cast<const unsigned char*>(iv.c_str())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize decryption");
    }
    
    // 分配输出缓冲区
    std::vector<unsigned char> plaintext(decoded_ciphertext.length() + AES_BLOCK_SIZE);
    int len = 0;
    int plaintext_len = 0;
    
    // 解密数据
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                          reinterpret_cast<const unsigned char*>(decoded_ciphertext.c_str()),
                          decoded_ciphertext.length()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Decryption failed");
    }
    plaintext_len = len;
    
    // 完成解密（处理填充）
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Decryption finalization failed");
    }
    plaintext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    
    return std::string(plaintext.begin(), plaintext.begin() + plaintext_len);
}

std::string Crypto::aes256GcmEncrypt(
    const std::string& plaintext,
    const std::string& key,
    const std::string& iv,
    const std::string& aad,
    std::string& tag
) {
    if (key.length() != 32) {
        throw std::invalid_argument("AES-256 requires 32-byte key");
    }
    if (iv.length() != 12) {
        throw std::invalid_argument("AES-GCM recommends 12-byte IV");
    }
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }
    
    // 初始化加密操作
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize GCM encryption");
    }
    
    // 设置IV长度
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.length(), nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set IV length");
    }
    
    // 设置密钥和IV
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(key.c_str()),
                           reinterpret_cast<const unsigned char*>(iv.c_str())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set key and IV");
    }
    
    // 设置AAD（附加认证数据）
    int len = 0;
    if (!aad.empty()) {
        if (EVP_EncryptUpdate(ctx, nullptr, &len,
                              reinterpret_cast<const unsigned char*>(aad.c_str()),
                              aad.length()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to set AAD");
        }
    }
    
    // 加密数据
    std::vector<unsigned char> ciphertext(plaintext.length());
    int ciphertext_len = 0;
    
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                          reinterpret_cast<const unsigned char*>(plaintext.c_str()),
                          plaintext.length()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("GCM encryption failed");
    }
    ciphertext_len = len;
    
    // 完成加密
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("GCM encryption finalization failed");
    }
    ciphertext_len += len;
    
    // 获取认证标签
    unsigned char tag_buf[16];
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag_buf) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to get authentication tag");
    }
    tag = std::string(reinterpret_cast<char*>(tag_buf), 16);
    
    EVP_CIPHER_CTX_free(ctx);
    
    return base64Encode(std::string(ciphertext.begin(), ciphertext.begin() + ciphertext_len));
}

std::string Crypto::aes256GcmDecrypt(
    const std::string& ciphertext,
    const std::string& key,
    const std::string& iv,
    const std::string& aad,
    const std::string& tag
) {
    if (key.length() != 32) {
        throw std::invalid_argument("AES-256 requires 32-byte key");
    }
    if (iv.length() != 12) {
        throw std::invalid_argument("AES-GCM recommends 12-byte IV");
    }
    if (tag.length() != 16) {
        throw std::invalid_argument("Authentication tag must be 16 bytes");
    }
    
    std::string decoded_ciphertext = base64Decode(ciphertext);
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create cipher context");
    }
    
    // 初始化解密操作
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize GCM decryption");
    }
    
    // 设置IV长度
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.length(), nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set IV length");
    }
    
    // 设置密钥和IV
    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast<const unsigned char*>(key.c_str()),
                           reinterpret_cast<const unsigned char*>(iv.c_str())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set key and IV");
    }
    
    // 设置AAD
    int len = 0;
    if (!aad.empty()) {
        if (EVP_DecryptUpdate(ctx, nullptr, &len,
                              reinterpret_cast<const unsigned char*>(aad.c_str()),
                              aad.length()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to set AAD");
        }
    }
    
    // 解密数据
    std::vector<unsigned char> plaintext(decoded_ciphertext.length());
    int plaintext_len = 0;
    
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                          reinterpret_cast<const unsigned char*>(decoded_ciphertext.c_str()),
                          decoded_ciphertext.length()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("GCM decryption failed");
    }
    plaintext_len = len;
    
    // 设置认证标签
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                            const_cast<char*>(tag.c_str())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set authentication tag");
    }
    
    // 完成解密（验证认证标签）
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("GCM decryption finalization failed (authentication failed)");
    }
    plaintext_len += len;
    
    EVP_CIPHER_CTX_free(ctx);
    
    return std::string(plaintext.begin(), plaintext.begin() + plaintext_len);
}

// =====================================================
// 4. HMAC签名实现
// =====================================================

std::string Crypto::hmacSha256(const std::string& message, const std::string& key) {
    std::string raw = hmacSha256Raw(message, key);
    return hexEncode(raw, false);
}

std::string Crypto::hmacSha256Raw(const std::string& message, const std::string& key) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    HMAC(EVP_sha256(),
         key.c_str(), key.length(),
         reinterpret_cast<const unsigned char*>(message.c_str()), message.length(),
         hash, &hash_len);
    
    return std::string(reinterpret_cast<char*>(hash), hash_len);
}

std::string Crypto::hmacSha512(const std::string& message, const std::string& key) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    HMAC(EVP_sha512(),
         key.c_str(), key.length(),
         reinterpret_cast<const unsigned char*>(message.c_str()), message.length(),
         hash, &hash_len);
    
    return hexEncode(std::string(reinterpret_cast<char*>(hash), hash_len), false);
}

// =====================================================
// 5. 随机数生成实现
// =====================================================

std::vector<uint8_t> Crypto::randomBytes(size_t length) {
    std::vector<uint8_t> buffer(length);
    
    if (RAND_bytes(buffer.data(), length) != 1) {
        throw std::runtime_error("Failed to generate random bytes");
    }
    
    return buffer;
}

std::string Crypto::randomString(size_t length) {
    static const char charset[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789";
    
    std::vector<uint8_t> random_data = randomBytes(length);
    std::string result;
    result.reserve(length);
    
    for (uint8_t byte : random_data) {
        result += charset[byte % (sizeof(charset) - 1)];
    }
    
    return result;
}

std::string Crypto::randomHex(size_t length) {
    size_t byte_count = (length + 1) / 2;
    std::vector<uint8_t> random_data = randomBytes(byte_count);
    std::string hex = hexEncode(std::string(random_data.begin(), random_data.end()), false);
    return hex.substr(0, length);
}

std::string Crypto::generateSalt(size_t length) {
    std::vector<uint8_t> salt_bytes = randomBytes(length);
    return base64Encode(std::string(salt_bytes.begin(), salt_bytes.end()));
}

std::string Crypto::generateUuid() {
    std::vector<uint8_t> uuid_bytes = randomBytes(16);
    
    // 设置版本号（v4）和变体
    uuid_bytes[6] = (uuid_bytes[6] & 0x0F) | 0x40;  // Version 4
    uuid_bytes[8] = (uuid_bytes[8] & 0x3F) | 0x80;  // Variant 10xx
    
    // 格式化为UUID字符串
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    
    for (size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::setw(2) << static_cast<int>(uuid_bytes[i]);
    }
    
    return oss.str();
}

// =====================================================
// 6. 密钥派生实现
// =====================================================

std::string Crypto::pbkdf2Sha256(
    const std::string& password,
    const std::string& salt,
    int iterations,
    size_t key_length
) {
    std::vector<unsigned char> derived_key(key_length);
    
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), password.length(),
            reinterpret_cast<const unsigned char*>(salt.c_str()), salt.length(),
            iterations,
            EVP_sha256(),
            key_length,
            derived_key.data()) != 1) {
        throw std::runtime_error("PBKDF2 key derivation failed");
    }
    
    return hexEncode(std::string(derived_key.begin(), derived_key.end()), false);
}

// =====================================================
// 7. 工具方法实现
// =====================================================

bool Crypto::isValidBase64(const std::string& input) {
    if (input.empty()) {
        return false;
    }
    
    // 检查长度是否是4的倍数
    if (input.length() % 4 != 0) {
        return false;
    }
    
    // 检查字符是否合法
    for (char c : input) {
        if (!std::isalnum(c) && c != '+' && c != '/' && c != '=') {
            return false;
        }
    }
    
    return true;
}

bool Crypto::isValidHex(const std::string& input) {
    if (input.empty()) {
        return false;
    }
    
    // 检查所有字符是否为十六进制字符
    for (char c : input) {
        if (!std::isxdigit(c)) {
            return false;
        }
    }
    
    return true;
}

} // namespace utils
} // namespace common
