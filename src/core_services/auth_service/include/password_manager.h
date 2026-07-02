/**
 * @file password_manager.h
 * @brief 密码管理器 - 负责密码的哈希、验证和安全管理
 * @author 29108
 * @date 2025/7/21
 * @version 1.0
 *
 * 功能特性:
 * - 密码安全哈希（bcrypt算法）
 * - 密码强度验证
 * - 盐值生成和管理
 * - 密码验证
 * - 随机密码生成
 * - 密码策略配置
 *
 * 安全特性:
 * - 使用bcrypt算法进行密码哈希
 * - 自动盐值生成
 * - 可配置的哈希成本因子
 * - 密码强度检查
 * - 防止时序攻击
 *
 * 与现有模块集成:
 * - 使用ConfigManager进行配置管理
 * - 使用Logger进行日志记录
 * - 集成ThreadPool进行异步处理
 */

#ifndef PASSWORD_MANAGER_H
#define PASSWORD_MANAGER_H

#include "common/config/config_manager.h"
#include "common/logger/logger.h"
#include <string>
#include <vector>
#include <regex>
#include <memory>
#include <chrono>
#include <atomic>

namespace core_services {
    namespace auth_service {

        /**
         * @brief 密码验证结果
         */
        struct PasswordValidationResult {
            bool valid = false;                                     ///< 是否有效
            std::string error_message;                              ///< 错误消息
            std::vector<std::string> failed_rules;                  ///< 失败的规则列表
            int strength_score = 0;                                 ///< 强度评分（0-100）
            std::string strength_level;                             ///< 强度等级（弱/中/强）

            /**
             * @brief 默认构造函数
             */
            PasswordValidationResult() = default;

            /**
             * @brief 成功结果构造函数
             * @param strength_score 强度评分
             * @param strength_level 强度等级
             */
            PasswordValidationResult(int strength_score, const std::string& strength_level);

            /**
             * @brief 失败结果构造函数
             * @param error_message 错误消息
             * @param failed_rules 失败的规则列表
             */
            PasswordValidationResult(const std::string& error_message, const std::vector<std::string>& failed_rules);
        };

        /**
         * @brief 密码哈希结果
         */
        struct PasswordHashResult {
            bool success = false;                                   ///< 是否成功
            std::string hash;                                       ///< 密码哈希值
            std::string salt;                                       ///< 盐值
            std::string error_message;                              ///< 错误消息
            std::chrono::milliseconds hash_time{0};                 ///< 哈希耗时

            /**
             * @brief 默认构造函数
             */
            PasswordHashResult() = default;

            /**
             * @brief 成功结果构造函数
             * @param hash 密码哈希值
             * @param salt 盐值
             * @param hash_time 哈希耗时
             */
            PasswordHashResult(const std::string& hash, const std::string& salt, std::chrono::milliseconds hash_time);

            /**
             * @brief 失败结果构造函数
             * @param error_message 错误消息
             */
            explicit PasswordHashResult(const std::string& error_message);
        };

        /**
         * @brief 密码管理器类
         * @details 负责密码的安全管理和验证
         */
        class PasswordManager {
        public:
            /**
             * @brief 密码策略配置
             */
            struct Config {
                int min_length = 8;                                 ///< 最小长度
                int max_length = 128;                               ///< 最大长度
                bool require_uppercase = true;                      ///< 是否需要大写字母
                bool require_lowercase = true;                      ///< 是否需要小写字母
                bool require_digits = true;                         ///< 是否需要数字
                bool require_special_chars = true;                  ///< 是否需要特殊字符
                int min_special_chars = 1;                          ///< 最少特殊字符数量
                int min_digits = 1;                                 ///< 最少数字数量
                int min_uppercase = 1;                              ///< 最少大写字母数量
                int min_lowercase = 1;                              ///< 最少小写字母数量
                bool forbid_common_passwords = true;                ///< 是否禁止常见密码
                bool forbid_personal_info = true;                   ///< 是否禁止包含个人信息
                int bcrypt_cost = 12;                               ///< bcrypt成本因子（4-31）
                std::string special_chars = "!@#$%^&*()_+-=[]{}|;:,.<>?"; ///< 允许的特殊字符
                std::vector<std::string> forbidden_passwords;       ///< 禁止的密码列表

                /**
                 * @brief 从配置管理器加载配置
                 * @return 密码管理器配置对象
                 */
                static Config fromConfigManager();

                /**
                 * @brief 验证配置有效性
                 * @return 验证结果，成功返回空字符串
                 */
                std::string validate() const;
            };

            /**
             * @brief 构造函数
             * @param config 密码管理器配置
             */
            explicit PasswordManager(const Config& config);

            /**
             * @brief 析构函数
             */
            ~PasswordManager();

            // ==================== 密码哈希和验证 ====================

            /**
             * @brief 哈希密码
             * @param password 明文密码
             * @return 哈希结果
             */
            PasswordHashResult hashPassword(const std::string& password);

            /**
             * @brief 验证密码
             * @param password 明文密码
             * @param hash 密码哈希值
             * @return 验证成功返回true
             */
            bool verifyPassword(const std::string& password, const std::string& hash);

            /**
             * @brief 验证密码（带盐值）
             * @param password 明文密码
             * @param hash 密码哈希值
             * @param salt 盐值
             * @return 验证成功返回true
             */
            bool verifyPassword(const std::string& password, const std::string& hash, const std::string& salt);

            /**
             * @brief 验证密码（带详细结果）
             * @param password 明文密码
             * @param hash 密码哈希值
             * @return 验证结果和耗时
             */
            std::pair<bool, std::chrono::milliseconds> verifyPasswordWithTiming(const std::string& password, const std::string& hash);

            // ==================== 密码强度验证 ====================

            /**
             * @brief 验证密码强度
             * @param password 密码
             * @param username 用户名（可选，用于检查是否包含个人信息）
             * @param email 邮箱（可选，用于检查是否包含个人信息）
             * @return 验证结果
             */
            PasswordValidationResult validatePasswordStrength(const std::string& password,
                                                             const std::string& username = "",
                                                             const std::string& email = "");

            /**
             * @brief 计算密码强度评分
             * @param password 密码
             * @return 强度评分（0-100）
             */
            int calculatePasswordStrength(const std::string& password);

            /**
             * @brief 检查密码是否为常见密码
             * @param password 密码
             * @return 如果是常见密码返回true
             */
            bool isCommonPassword(const std::string& password);

            // ==================== 密码生成 ====================

            /**
             * @brief 生成随机密码
             * @param length 密码长度
             * @param include_uppercase 是否包含大写字母
             * @param include_lowercase 是否包含小写字母
             * @param include_digits 是否包含数字
             * @param include_special 是否包含特殊字符
             * @return 生成的密码
             */
            std::string generateRandomPassword(int length = 16,
                                             bool include_uppercase = true,
                                             bool include_lowercase = true,
                                             bool include_digits = true,
                                             bool include_special = true);

            /**
             * @brief 生成符合策略的随机密码
             * @return 生成的密码
             */
            std::string generateCompliantPassword();

            // ==================== 配置和统计 ====================

            /**
             * @brief 更新配置
             * @param new_config 新配置
             * @return 更新成功返回true
             */
            bool updateConfig(const Config& new_config);

            /**
             * @brief 获取当前配置
             * @return 当前配置
             */
            const Config& getConfig() const { return config_; }

            /**
             * @brief 获取统计信息
             * @return 统计信息JSON对象
             */
            nlohmann::json getStatistics() const;

            /**
             * @brief 重置统计信息
             */
            void resetStatistics();

        private:
            Config config_;                                         ///< 密码策略配置

            // 统计信息
            mutable std::atomic<uint64_t> passwords_hashed_{0};     ///< 哈希的密码数量
            mutable std::atomic<uint64_t> passwords_verified_{0};   ///< 验证的密码数量
            mutable std::atomic<uint64_t> verification_failures_{0}; ///< 验证失败次数
            mutable std::atomic<uint64_t> strength_checks_{0};      ///< 强度检查次数
            mutable std::atomic<uint64_t> weak_passwords_{0};       ///< 弱密码数量

            // ==================== 内部方法 ====================

            /**
             * @brief 生成盐值
             * @return 盐值字符串
             */
            std::string generateSalt();

            /**
             * @brief 使用bcrypt哈希密码
             * @param password 明文密码
             * @param salt 盐值
             * @return 哈希值
             */
            std::string bcryptHash(const std::string& password, const std::string& salt);

            /**
             * @brief 使用bcrypt验证密码
             * @param password 明文密码
             * @param hash 哈希值
             * @return 验证成功返回true
             */
            bool bcryptVerify(const std::string& password, const std::string& hash);

            /**
             * @brief 使用bcrypt验证密码（带盐值）
             * @param password 明文密码
             * @param hash 哈希值
             * @param salt 盐值
             * @return 验证成功返回true
             */
            bool bcryptVerifyWithSalt(const std::string& password, const std::string& hash, const std::string& salt);

            /**
             * @brief 检查密码长度
             * @param password 密码
             * @return 检查通过返回true
             */
            bool checkPasswordLength(const std::string& password);

            /**
             * @brief 检查字符类型要求
             * @param password 密码
             * @return 失败的规则列表
             */
            std::vector<std::string> checkCharacterRequirements(const std::string& password);

            /**
             * @brief 检查是否包含个人信息
             * @param password 密码
             * @param username 用户名
             * @param email 邮箱
             * @return 如果包含个人信息返回true
             */
            bool containsPersonalInfo(const std::string& password, const std::string& username, const std::string& email);

            /**
             * @brief 加载常见密码列表
             */
            void loadCommonPasswords();

            /**
             * @brief 计算字符集大小
             * @param include_uppercase 是否包含大写字母
             * @param include_lowercase 是否包含小写字母
             * @param include_digits 是否包含数字
             * @param include_special 是否包含特殊字符
             * @return 字符集大小
             */
            int calculateCharsetSize(bool include_uppercase, bool include_lowercase,
                                   bool include_digits, bool include_special);

            // 常见密码列表（静态成员，所有实例共享）
            static std::vector<std::string> common_passwords_;
            static bool common_passwords_loaded_;
        };

    } // namespace auth_service
} // namespace core_services

#endif //PASSWORD_MANAGER_H
