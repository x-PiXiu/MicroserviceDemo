/**
 * @file password_manager.cpp
 * @brief 密码管理器实现 - 负责密码的哈希、验证和安全管理
 * @author 29108
 * @date 2025/7/21
 * @version 1.0
 */

#include "password_manager.h"
#include "user_models.h"
#include "common/logger/logger.h"
#include <bcrypt/BCrypt.hpp>
#include <random>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace core_services {
    namespace auth_service {

        // 静态成员初始化
        std::vector<std::string> PasswordManager::common_passwords_;
        bool PasswordManager::common_passwords_loaded_ = false;

        // ==================== PasswordValidationResult 实现 ====================

        /**
         * @brief 成功结果构造函数
         * @param strength_score 强度评分
         * @param strength_level 强度等级
         */
        PasswordValidationResult::PasswordValidationResult(int strength_score, const std::string& strength_level)
            : valid(true)
            , strength_score(strength_score)
            , strength_level(strength_level) {
        }

        /**
         * @brief 失败结果构造函数
         * @param error_message 错误消息
         * @param failed_rules 失败的规则列表
         */
        PasswordValidationResult::PasswordValidationResult(const std::string& error_message,
                                                          const std::vector<std::string>& failed_rules)
            : valid(false)
            , error_message(error_message)
            , failed_rules(failed_rules) {
        }

        // ==================== PasswordHashResult 实现 ====================

        /**
         * @brief 成功结果构造函数
         * @param hash 密码哈希值
         * @param salt 盐值
         * @param hash_time 哈希耗时
         */
        PasswordHashResult::PasswordHashResult(const std::string& hash, const std::string& salt,
                                              std::chrono::milliseconds hash_time)
            : success(true)
            , hash(hash)
            , salt(salt)
            , hash_time(hash_time) {
        }

        /**
         * @brief 失败结果构造函数
         * @param error_message 错误消息
         */
        PasswordHashResult::PasswordHashResult(const std::string& error_message)
            : success(false)
            , error_message(error_message) {
        }

        // ==================== PasswordManager::Config 实现 ====================

        /**
         * @brief 从配置管理器加载配置
         * @return 密码管理器配置对象
         */
        PasswordManager::Config PasswordManager::Config::fromConfigManager() {
            auto& config_manager = common::config::ConfigManager::getInstance();

            Config config;
            config.min_length = config_manager.get<int>("password.min_length", config.min_length);
            config.max_length = config_manager.get<int>("password.max_length", config.max_length);
            config.require_uppercase = config_manager.get<bool>("password.require_uppercase", config.require_uppercase);
            config.require_lowercase = config_manager.get<bool>("password.require_lowercase", config.require_lowercase);
            config.require_digits = config_manager.get<bool>("password.require_digits", config.require_digits);
            config.require_special_chars = config_manager.get<bool>("password.require_special_chars", config.require_special_chars);
            config.min_special_chars = config_manager.get<int>("password.min_special_chars", config.min_special_chars);
            config.min_digits = config_manager.get<int>("password.min_digits", config.min_digits);
            config.min_uppercase = config_manager.get<int>("password.min_uppercase", config.min_uppercase);
            config.min_lowercase = config_manager.get<int>("password.min_lowercase", config.min_lowercase);
            config.forbid_common_passwords = config_manager.get<bool>("password.forbid_common_passwords", config.forbid_common_passwords);
            config.forbid_personal_info = config_manager.get<bool>("password.forbid_personal_info", config.forbid_personal_info);
            config.bcrypt_cost = config_manager.get<int>("password.bcrypt_cost", config.bcrypt_cost);
            config.special_chars = config_manager.get<std::string>("password.special_chars", config.special_chars);

            return config;
        }

        /**
         * @brief 验证配置有效性
         * @return 验证结果，成功返回空字符串
         */
        std::string PasswordManager::Config::validate() const {
            if (min_length < 1) {
                return "密码最小长度必须大于0";
            }

            if (max_length < min_length) {
                return "密码最大长度不能小于最小长度";
            }

            if (max_length > 1024) {
                return "密码最大长度不能超过1024个字符";
            }

            if (bcrypt_cost < 4 || bcrypt_cost > 31) {
                return "bcrypt成本因子必须在4-31之间";
            }

            if (min_special_chars < 0 || min_digits < 0 || min_uppercase < 0 || min_lowercase < 0) {
                return "字符数量要求不能为负数";
            }

            // 检查字符要求是否合理
            int total_required = min_special_chars + min_digits + min_uppercase + min_lowercase;
            if (total_required > min_length) {
                return "各类字符的最小数量要求总和不能超过密码最小长度";
            }

            return ""; // 验证通过
        }

        // ==================== PasswordManager 实现 ====================

        /**
         * @brief 构造函数
         * @param config 密码管理器配置
         */
        PasswordManager::PasswordManager(const Config& config)
            : config_(config) {

            // 验证配置
            std::string validation_error = config_.validate();
            if (!validation_error.empty()) {
                throw std::invalid_argument("密码管理器配置无效: " + validation_error);
            }

            // 加载常见密码列表
            if (!common_passwords_loaded_) {
                loadCommonPasswords();
            }

            LOG_INFO("密码管理器初始化完成");
            LOG_INFO("- 最小长度: " + std::to_string(config_.min_length));
            LOG_INFO("- 最大长度: " + std::to_string(config_.max_length));
            LOG_INFO("- bcrypt成本: " + std::to_string(config_.bcrypt_cost));
            LOG_INFO(std::string("- 禁止常见密码: ") + (config_.forbid_common_passwords ? "是" : "否"));
        }

        /**
         * @brief 析构函数
         */
        PasswordManager::~PasswordManager() {
            LOG_INFO("密码管理器销毁");
        }

        /**
         * @brief 哈希密码
         * @param password 明文密码
         * @return 哈希结果
         */
        PasswordHashResult PasswordManager::hashPassword(const std::string& password) {
            try {
                auto start_time = std::chrono::high_resolution_clock::now();

                // 验证密码强度
                auto validation_result = validatePasswordStrength(password);
                if (!validation_result.valid) {
                    return PasswordHashResult("密码不符合安全要求: " + validation_result.error_message);
                }

                // 直接使用BCrypt哈希密码（BCrypt内部会自动生成和管理盐值）
                LOG_DEBUG("开始生成密码哈希 - 密码长度: " + std::to_string(password.length()) +
                         ", BCrypt成本: " + std::to_string(config_.bcrypt_cost));

                std::string hash = BCrypt::generateHash(password, config_.bcrypt_cost);

                // BCrypt不需要额外的盐值，返回空字符串作为盐值
                std::string salt = "";

                auto end_time = std::chrono::high_resolution_clock::now();
                auto hash_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                passwords_hashed_++;
                LOG_DEBUG("密码哈希成功，耗时: " + std::to_string(hash_time.count()) + "ms");
                LOG_DEBUG("BCrypt哈希值长度: " + std::to_string(hash.length()));
                LOG_DEBUG(std::string("生成的哈希前缀: ") + (hash.length() > 10 ? hash.substr(0, 10) : hash));

                return PasswordHashResult(hash, salt, hash_time);
            } catch (const std::exception& e) {
                LOG_ERROR("密码哈希失败: " + std::string(e.what()));
                return PasswordHashResult("密码哈希失败: " + std::string(e.what()));
            }
        }

        /**
         * @brief 验证密码
         * @param password 明文密码
         * @param hash 密码哈希值
         * @return 验证成功返回true
         */
        bool PasswordManager::verifyPassword(const std::string& password, const std::string& hash) {
            try {
                passwords_verified_++;

                // 添加调试日志
                LOG_DEBUG("密码验证开始 - 密码长度: " + std::to_string(password.length()) +
                         ", 哈希长度: " + std::to_string(hash.length()));
                LOG_DEBUG(std::string("哈希值前缀: ") + (hash.length() > 10 ? hash.substr(0, 10) : hash));

                bool result = bcryptVerify(password, hash);
                if (!result) {
                    verification_failures_++;
                    LOG_WARNING("密码验证失败 - 密码: [隐藏], 哈希: " +
                               (hash.length() > 20 ? hash.substr(0, 20) + "..." : hash));
                } else {
                    LOG_DEBUG("密码验证成功");
                }

                return result;
            } catch (const std::exception& e) {
                verification_failures_++;
                LOG_ERROR("密码验证失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 验证密码（带盐值）
         * @param password 明文密码
         * @param hash 密码哈希值
         * @param salt 盐值（对于BCrypt，此参数被忽略，因为盐值已包含在hash中）
         * @return 验证成功返回true
         */
        bool PasswordManager::verifyPassword(const std::string& password, const std::string& hash, const std::string& salt) {
            try {
                passwords_verified_++;

                // 对于BCrypt，直接验证密码和哈希值，忽略额外的盐值参数
                bool result = BCrypt::validatePassword(password, hash);
                if (!result) {
                    verification_failures_++;
                    LOG_DEBUG("密码验证失败 - 原始密码长度: " + std::to_string(password.length()));
                    LOG_DEBUG("密码验证失败 - 哈希值长度: " + std::to_string(hash.length()));
                    LOG_DEBUG("密码验证失败 - 盐值参数（已忽略）: " + salt);
                } else {
                    LOG_DEBUG("密码验证成功");
                }

                return result;
            } catch (const std::exception& e) {
                verification_failures_++;
                LOG_ERROR("密码验证失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 验证密码（带详细结果）
         * @param password 明文密码
         * @param hash 密码哈希值
         * @return 验证结果和耗时
         */
        std::pair<bool, std::chrono::milliseconds> PasswordManager::verifyPasswordWithTiming(
            const std::string& password, const std::string& hash) {
            try {
                auto start_time = std::chrono::high_resolution_clock::now();

                bool result = verifyPassword(password, hash);

                auto end_time = std::chrono::high_resolution_clock::now();
                auto verify_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

                return std::make_pair(result, verify_time);
            } catch (const std::exception& e) {
                LOG_ERROR("密码验证失败: " + std::string(e.what()));
                return std::make_pair(false, std::chrono::milliseconds(0));
            }
        }

        /**
         * @brief 验证密码强度
         * @param password 密码
         * @param username 用户名（可选，用于检查是否包含个人信息）
         * @param email 邮箱（可选，用于检查是否包含个人信息）
         * @return 验证结果
         */
        PasswordValidationResult PasswordManager::validatePasswordStrength(const std::string& password,
                                                                          const std::string& username,
                                                                          const std::string& email) {
            try {
                strength_checks_++;

                std::vector<std::string> failed_rules;

                // 检查密码长度
                if (!checkPasswordLength(password)) {
                    failed_rules.push_back("密码长度必须在" + std::to_string(config_.min_length) +
                                          "-" + std::to_string(config_.max_length) + "个字符之间");
                }

                // 检查字符类型要求
                auto char_failures = checkCharacterRequirements(password);
                failed_rules.insert(failed_rules.end(), char_failures.begin(), char_failures.end());

                // 检查是否为常见密码
                if (config_.forbid_common_passwords && isCommonPassword(password)) {
                    failed_rules.push_back("不能使用常见密码");
                }

                // 检查是否包含个人信息
                if (config_.forbid_personal_info && containsPersonalInfo(password, username, email)) {
                    failed_rules.push_back("密码不能包含用户名或邮箱信息");
                }

                // 如果有失败的规则，返回失败结果
                if (!failed_rules.empty()) {
                    weak_passwords_++;
                    std::string error_message = "密码不符合安全要求";
                    return PasswordValidationResult(error_message, failed_rules);
                }

                // 计算密码强度
                int strength_score = calculatePasswordStrength(password);
                std::string strength_level;

                if (strength_score >= 80) {
                    strength_level = "强";
                } else if (strength_score >= 60) {
                    strength_level = "中";
                } else {
                    strength_level = "弱";
                    weak_passwords_++;
                }

                return PasswordValidationResult(strength_score, strength_level);
            } catch (const std::exception& e) {
                LOG_ERROR("密码强度验证失败: " + std::string(e.what()));
                return PasswordValidationResult("密码强度验证失败", {"系统错误"});
            }
        }

        /**
         * @brief 计算密码强度评分
         * @param password 密码
         * @return 强度评分（0-100）
         */
        int PasswordManager::calculatePasswordStrength(const std::string& password) {
            int score = 0;

            // 长度评分（最多30分）
            int length = static_cast<int>(password.length());
            if (length >= 12) {
                score += 30;
            } else if (length >= 8) {
                score += 20;
            } else if (length >= 6) {
                score += 10;
            }

            // 字符类型多样性评分（最多40分）
            bool has_lower = false, has_upper = false, has_digit = false, has_special = false;

            for (char c : password) {
                if (std::islower(c)) has_lower = true;
                else if (std::isupper(c)) has_upper = true;
                else if (std::isdigit(c)) has_digit = true;
                else if (config_.special_chars.find(c) != std::string::npos) has_special = true;
            }

            int char_types = (has_lower ? 1 : 0) + (has_upper ? 1 : 0) +
                           (has_digit ? 1 : 0) + (has_special ? 1 : 0);
            score += char_types * 10;

            // 字符重复度评分（最多20分）
            std::unordered_map<char, int> char_count;
            for (char c : password) {
                char_count[c]++;
            }

            int max_repeat = 0;
            for (const auto& pair : char_count) {
                max_repeat = std::max(max_repeat, pair.second);
            }

            double repeat_ratio = static_cast<double>(max_repeat) / password.length();
            if (repeat_ratio <= 0.2) {
                score += 20;
            } else if (repeat_ratio <= 0.4) {
                score += 10;
            }

            // 模式复杂度评分（最多10分）
            bool has_pattern = false;

            // 检查简单的递增/递减模式
            for (size_t i = 0; i < password.length() - 2; ++i) {
                if ((password[i] + 1 == password[i+1] && password[i+1] + 1 == password[i+2]) ||
                    (password[i] - 1 == password[i+1] && password[i+1] - 1 == password[i+2])) {
                    has_pattern = true;
                    break;
                }
            }

            if (!has_pattern) {
                score += 10;
            }

            return std::min(100, score);
        }

        /**
         * @brief 检查密码是否为常见密码
         * @param password 密码
         * @return 如果是常见密码返回true
         */
        bool PasswordManager::isCommonPassword(const std::string& password) {
            if (!common_passwords_loaded_) {
                return false;
            }

            // 转换为小写进行比较
            std::string lower_password = password;
            std::transform(lower_password.begin(), lower_password.end(), lower_password.begin(), ::tolower);

            return std::find(common_passwords_.begin(), common_passwords_.end(), lower_password) != common_passwords_.end();
        }

        /**
         * @brief 生成随机密码
         * @param length 密码长度
         * @param include_uppercase 是否包含大写字母
         * @param include_lowercase 是否包含小写字母
         * @param include_digits 是否包含数字
         * @param include_special 是否包含特殊字符
         * @return 生成的密码
         */
        std::string PasswordManager::generateRandomPassword(int length,
                                                           bool include_uppercase,
                                                           bool include_lowercase,
                                                           bool include_digits,
                                                           bool include_special) {
            if (length <= 0) {
                throw std::invalid_argument("密码长度必须大于0");
            }

            std::string charset;

            if (include_lowercase) {
                charset += "abcdefghijklmnopqrstuvwxyz";
            }
            if (include_uppercase) {
                charset += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            }
            if (include_digits) {
                charset += "0123456789";
            }
            if (include_special) {
                charset += config_.special_chars;
            }

            if (charset.empty()) {
                throw std::invalid_argument("至少需要包含一种字符类型");
            }

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, charset.size() - 1);

            std::string password;
            password.reserve(length);

            for (int i = 0; i < length; ++i) {
                password += charset[dis(gen)];
            }

            return password;
        }

        /**
         * @brief 生成符合策略的随机密码
         * @return 生成的密码
         */
        std::string PasswordManager::generateCompliantPassword() {
            int length = std::max(config_.min_length, 12); // 至少12位

            // 确保包含所有必需的字符类型
            std::string password;
            std::random_device rd;
            std::mt19937 gen(rd());

            // 先添加必需的字符
            if (config_.require_lowercase) {
                std::string lowercase = "abcdefghijklmnopqrstuvwxyz";
                std::uniform_int_distribution<> dis(0, lowercase.size() - 1);
                for (int i = 0; i < config_.min_lowercase; ++i) {
                    password += lowercase[dis(gen)];
                }
            }

            if (config_.require_uppercase) {
                std::string uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
                std::uniform_int_distribution<> dis(0, uppercase.size() - 1);
                for (int i = 0; i < config_.min_uppercase; ++i) {
                    password += uppercase[dis(gen)];
                }
            }

            if (config_.require_digits) {
                std::string digits = "0123456789";
                std::uniform_int_distribution<> dis(0, digits.size() - 1);
                for (int i = 0; i < config_.min_digits; ++i) {
                    password += digits[dis(gen)];
                }
            }

            if (config_.require_special_chars) {
                std::uniform_int_distribution<> dis(0, config_.special_chars.size() - 1);
                for (int i = 0; i < config_.min_special_chars; ++i) {
                    password += config_.special_chars[dis(gen)];
                }
            }

            // 填充剩余长度
            int remaining = length - static_cast<int>(password.length());
            if (remaining > 0) {
                std::string additional = generateRandomPassword(remaining,
                    config_.require_uppercase, config_.require_lowercase,
                    config_.require_digits, config_.require_special_chars);
                password += additional;
            }

            // 随机打乱密码字符顺序
            std::shuffle(password.begin(), password.end(), gen);

            return password;
        }

        /**
         * @brief 获取统计信息
         * @return 统计信息JSON对象
         */
        nlohmann::json PasswordManager::getStatistics() const {
            nlohmann::json stats;

            stats["passwords_hashed"] = passwords_hashed_.load();
            stats["passwords_verified"] = passwords_verified_.load();
            stats["verification_failures"] = verification_failures_.load();
            stats["strength_checks"] = strength_checks_.load();
            stats["weak_passwords"] = weak_passwords_.load();

            // 计算成功率
            uint64_t total_verifications = passwords_verified_.load();
            if (total_verifications > 0) {
                double success_rate = 1.0 - (static_cast<double>(verification_failures_.load()) / total_verifications);
                stats["verification_success_rate"] = success_rate;
            } else {
                stats["verification_success_rate"] = 0.0;
            }

            // 计算弱密码比例
            uint64_t total_checks = strength_checks_.load();
            if (total_checks > 0) {
                double weak_ratio = static_cast<double>(weak_passwords_.load()) / total_checks;
                stats["weak_password_ratio"] = weak_ratio;
            } else {
                stats["weak_password_ratio"] = 0.0;
            }

            // 配置信息
            stats["config"] = {
                {"min_length", config_.min_length},
                {"max_length", config_.max_length},
                {"require_uppercase", config_.require_uppercase},
                {"require_lowercase", config_.require_lowercase},
                {"require_digits", config_.require_digits},
                {"require_special_chars", config_.require_special_chars},
                {"bcrypt_cost", config_.bcrypt_cost},
                {"forbid_common_passwords", config_.forbid_common_passwords},
                {"forbid_personal_info", config_.forbid_personal_info}
            };

            return stats;
        }

        /**
         * @brief 更新配置
         * @param new_config 新配置
         * @return 更新成功返回true
         */
        bool PasswordManager::updateConfig(const Config& new_config) {
            try {
                // 验证新配置的有效性
                if (new_config.min_length < 1 || new_config.min_length > new_config.max_length) {
                    LOG_ERROR("密码长度配置无效");
                    return false;
                }

                if (new_config.bcrypt_cost < 4 || new_config.bcrypt_cost > 31) {
                    LOG_ERROR("bcrypt成本因子配置无效，必须在4-31之间");
                    return false;
                }

                // 更新配置
                config_ = new_config;

                LOG_INFO("密码管理器配置更新成功");
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("更新密码管理器配置失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 重置统计信息
         */
        void PasswordManager::resetStatistics() {
            passwords_hashed_ = 0;
            passwords_verified_ = 0;
            verification_failures_ = 0;
            strength_checks_ = 0;
            weak_passwords_ = 0;

            LOG_INFO("密码管理器统计信息已重置");
        }

        // ==================== 内部方法实现 ====================

        /**
         * @brief 生成盐值
         * @return 盐值字符串
         */
        std::string PasswordManager::generateSalt() {
            return generateRandomString(16); // 16字节盐值
        }

        /**
         * @brief 使用bcrypt哈希密码
         * @param password 明文密码
         * @param salt 盐值（已弃用，BCrypt内部管理盐值）
         * @return 哈希值
         */
        std::string PasswordManager::bcryptHash(const std::string& password, const std::string& salt) {
            try {
                // 直接使用BCrypt库进行哈希，不添加额外盐值
                // BCrypt会自动生成和管理盐值
                (void)salt; // 消除未使用参数警告
                return BCrypt::generateHash(password, config_.bcrypt_cost);
            } catch (const std::exception& e) {
                LOG_ERROR("bcrypt哈希失败: " + std::string(e.what()));
                throw;
            }
        }

        /**
         * @brief 使用bcrypt验证密码
         * @param password 明文密码
         * @param hash 哈希值
         * @return 验证成功返回true
         */
        bool PasswordManager::bcryptVerify(const std::string& password, const std::string& hash) {
            try {
                // 添加调试日志
                LOG_DEBUG("BCrypt验证开始 - 密码长度: " + std::to_string(password.length()) +
                         ", 哈希长度: " + std::to_string(hash.length()));

                // 验证哈希格式是否正确（BCrypt哈希应该以$2a$、$2b$或$2y$开头）
                if (hash.length() < 60 || (hash.substr(0, 4) != "$2a$" && hash.substr(0, 4) != "$2b$" && hash.substr(0, 4) != "$2y$")) {
                    LOG_ERROR(std::string("无效的BCrypt哈希格式: ") + (hash.length() > 20 ? hash.substr(0, 20) + "..." : hash));
                    return false;
                }

                bool result = BCrypt::validatePassword(password, hash);
                LOG_DEBUG(std::string("BCrypt验证结果: ") + (result ? "成功" : "失败"));

                return result;
            } catch (const std::exception& e) {
                LOG_ERROR("bcrypt验证失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 使用bcrypt验证密码（带盐值）
         * @param password 明文密码
         * @param hash 哈希值
         * @param salt 盐值（已弃用，BCrypt内部管理盐值）
         * @return 验证成功返回true
         */
        bool PasswordManager::bcryptVerifyWithSalt(const std::string& password, const std::string& hash, const std::string& salt) {
            try {
                LOG_DEBUG("密码验证 - 原始密码长度: " + std::to_string(password.length()));
                LOG_DEBUG("密码验证 - 哈希值长度: " + std::to_string(hash.length()));
                LOG_DEBUG("密码验证 - 盐值参数（已忽略）: " + salt);

                // 直接使用BCrypt验证，不添加额外盐值
                // BCrypt哈希值中已包含盐值信息
                bool result = BCrypt::validatePassword(password, hash);
                LOG_DEBUG(std::string("密码验证结果: ") + (result ? "成功" : "失败"));

                return result;
            } catch (const std::exception& e) {
                LOG_ERROR("bcrypt带盐验证失败: " + std::string(e.what()));
                return false;
            }
        }

        /**
         * @brief 检查密码长度
         * @param password 密码
         * @return 检查通过返回true
         */
        bool PasswordManager::checkPasswordLength(const std::string& password) {
            int length = static_cast<int>(password.length());
            return length >= config_.min_length && length <= config_.max_length;
        }

        /**
         * @brief 检查字符类型要求
         * @param password 密码
         * @return 失败的规则列表
         */
        std::vector<std::string> PasswordManager::checkCharacterRequirements(const std::string& password) {
            std::vector<std::string> failures;

            int uppercase_count = 0, lowercase_count = 0, digit_count = 0, special_count = 0;

            for (char c : password) {
                if (std::isupper(c)) {
                    uppercase_count++;
                } else if (std::islower(c)) {
                    lowercase_count++;
                } else if (std::isdigit(c)) {
                    digit_count++;
                } else if (config_.special_chars.find(c) != std::string::npos) {
                    special_count++;
                }
            }

            if (config_.require_uppercase && uppercase_count < config_.min_uppercase) {
                failures.push_back("至少需要" + std::to_string(config_.min_uppercase) + "个大写字母");
            }

            if (config_.require_lowercase && lowercase_count < config_.min_lowercase) {
                failures.push_back("至少需要" + std::to_string(config_.min_lowercase) + "个小写字母");
            }

            if (config_.require_digits && digit_count < config_.min_digits) {
                failures.push_back("至少需要" + std::to_string(config_.min_digits) + "个数字");
            }

            if (config_.require_special_chars && special_count < config_.min_special_chars) {
                failures.push_back("至少需要" + std::to_string(config_.min_special_chars) + "个特殊字符");
            }

            return failures;
        }

        /**
         * @brief 检查是否包含个人信息
         * @param password 密码
         * @param username 用户名
         * @param email 邮箱
         * @return 如果包含个人信息返回true
         */
        bool PasswordManager::containsPersonalInfo(const std::string& password,
                                                  const std::string& username,
                                                  const std::string& email) {
            if (!config_.forbid_personal_info) {
                return false;
            }

            std::string lower_password = password;
            std::transform(lower_password.begin(), lower_password.end(), lower_password.begin(), ::tolower);

            // 检查用户名
            if (!username.empty()) {
                std::string lower_username = username;
                std::transform(lower_username.begin(), lower_username.end(), lower_username.begin(), ::tolower);

                if (lower_password.find(lower_username) != std::string::npos) {
                    return true;
                }
            }

            // 检查邮箱
            if (!email.empty()) {
                std::string lower_email = email;
                std::transform(lower_email.begin(), lower_email.end(), lower_email.begin(), ::tolower);

                // 检查完整邮箱
                if (lower_password.find(lower_email) != std::string::npos) {
                    return true;
                }

                // 检查邮箱用户名部分
                size_t at_pos = lower_email.find('@');
                if (at_pos != std::string::npos) {
                    std::string email_username = lower_email.substr(0, at_pos);
                    if (email_username.length() >= 3 && lower_password.find(email_username) != std::string::npos) {
                        return true;
                    }
                }
            }

            return false;
        }

        /**
         * @brief 加载常见密码列表
         */
        void PasswordManager::loadCommonPasswords() {
            try {
                // 内置一些常见密码
                common_passwords_ = {
                    "123456", "password", "123456789", "12345678", "12345",
                    "1234567", "1234567890", "qwerty", "abc123", "111111",
                    "123123", "admin", "letmein", "welcome", "monkey",
                    "dragon", "pass", "master", "hello", "freedom",
                    "whatever", "qazwsx", "trustno1", "jordan", "harley",
                    "1234", "robert", "matthew", "jordan23", "1qaz2wsx",
                    "superman", "fuckyou", "123qwe", "football", "baseball",
                    "liverpool", "princess", "abc123", "nicole", "daniel",
                    "babygirl", "monkey", "lovely", "jessica", "654321",
                    "michael", "ashley", "qwerty123", "iloveyou", "michelle"
                };

                // 尝试从文件加载更多常见密码
                std::ifstream file("config/common_passwords.txt");
                if (file.is_open()) {
                    std::string line;
                    while (std::getline(file, line)) {
                        if (!line.empty()) {
                            std::transform(line.begin(), line.end(), line.begin(), ::tolower);
                            common_passwords_.push_back(line);
                        }
                    }
                    file.close();
                    LOG_INFO("从文件加载了额外的常见密码");
                }

                // 去重
                std::sort(common_passwords_.begin(), common_passwords_.end());
                common_passwords_.erase(std::unique(common_passwords_.begin(), common_passwords_.end()),
                                       common_passwords_.end());

                common_passwords_loaded_ = true;
                LOG_INFO("常见密码列表加载完成，共" + std::to_string(common_passwords_.size()) + "个");

            } catch (const std::exception& e) {
                LOG_WARNING("加载常见密码列表失败: " + std::string(e.what()));
                common_passwords_loaded_ = true; // 即使失败也标记为已加载，避免重复尝试
            }
        }

        /**
         * @brief 计算字符集大小
         * @param include_uppercase 是否包含大写字母
         * @param include_lowercase 是否包含小写字母
         * @param include_digits 是否包含数字
         * @param include_special 是否包含特殊字符
         * @return 字符集大小
         */
        int PasswordManager::calculateCharsetSize(bool include_uppercase, bool include_lowercase,
                                                 bool include_digits, bool include_special) {
            int size = 0;

            if (include_lowercase) size += 26;
            if (include_uppercase) size += 26;
            if (include_digits) size += 10;
            if (include_special) size += static_cast<int>(config_.special_chars.length());

            return size;
        }

    } // namespace auth_service
} // namespace core_services
