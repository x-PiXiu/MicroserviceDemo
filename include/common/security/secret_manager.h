#pragma once

#include <string>
#include <optional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <cstdlib>

namespace common {
namespace security {

/**
 * 密钥来源类型
 */
enum class SecretSource {
    EnvironmentVariable,  // 环境变量
    File,                 // 文件（Docker/K8s Secrets）
    Vault,                // HashiCorp Vault
    AWS_SecretsManager,   // AWS Secrets Manager
    Azure_KeyVault,       // Azure Key Vault
    Kubernetes_Secret     // Kubernetes Secrets
};

/**
 * 密钥元数据
 */
struct SecretMetadata {
    std::string name;
    SecretSource source;
    std::chrono::system_clock::time_point loaded_at;
    std::chrono::seconds ttl{0};
    bool is_rotating{false};
};

/**
 * 密钥管理器接口
 */
class ISecretManager {
public:
    virtual ~ISecretManager() = default;

    /**
     * 获取密钥值
     * @param name 密钥名称
     * @return 密钥值，如果不存在返回 nullopt
     */
    virtual std::optional<std::string> getSecret(const std::string& name) = 0;

    /**
     * 获取密钥值和元数据
     */
    virtual std::optional<std::pair<std::string, SecretMetadata>>
        getSecretWithMetadata(const std::string& name) = 0;

    /**
     * 重新加载密钥（用于轮换）
     */
    virtual bool reloadSecret(const std::string& name) = 0;

    /**
     * 重新加载所有密钥
     */
    virtual void reloadAll() = 0;

    /**
     * 检查密钥是否存在
     */
    virtual bool hasSecret(const std::string& name) const = 0;

    /**
     * 验证必需的密钥
     * @param required_secrets 必需的密钥列表
     * @return 所有密钥都存在且非空返回 true
     */
    virtual bool validateRequiredSecrets(
        const std::vector<std::string>& required_secrets) = 0;
};

/**
 * 基于环境变量的密钥管理器
 */
class EnvironmentSecretManager : public ISecretManager {
public:
    /**
     * @param env_mappings 环境变量映射（逻辑名称 -> 环境变量名）
     *                     例如: {"jwt_secret", "JWT_SECRET_KEY"}
     */
    explicit EnvironmentSecretManager(
        const std::unordered_map<std::string, std::string>& env_mappings = {});

    std::optional<std::string> getSecret(const std::string& name) override;
    std::optional<std::pair<std::string, SecretMetadata>>
        getSecretWithMetadata(const std::string& name) override;
    bool reloadSecret(const std::string& name) override;
    void reloadAll() override;
    bool hasSecret(const std::string& name) const override;
    bool validateRequiredSecrets(
        const std::vector<std::string>& required_secrets) override;

    /**
     * 添加环境变量映射
     */
    void addMapping(const std::string& logical_name, const std::string& env_var);

private:
    std::unordered_map<std::string, std::string> env_mappings_;
    mutable std::unordered_map<std::string, std::pair<std::string, SecretMetadata>> cache_;
    mutable std::mutex mutex_;

    std::string getEnvVar(const std::string& name) const;
    std::string resolveEnvName(const std::string& logical_name) const;
};

/**
 * 基于文件的密钥管理器（用于 Docker Secrets, Kubernetes Secrets）
 */
class FileSecretManager : public ISecretManager {
public:
    /**
     * @param secrets_dir 密钥文件目录，默认为 Docker Secrets 目录
     */
    explicit FileSecretManager(const std::string& secrets_dir = "/run/secrets");

    std::optional<std::string> getSecret(const std::string& name) override;
    std::optional<std::pair<std::string, SecretMetadata>>
        getSecretWithMetadata(const std::string& name) override;
    bool reloadSecret(const std::string& name) override;
    void reloadAll() override;
    bool hasSecret(const std::string& name) const override;
    bool validateRequiredSecrets(
        const std::vector<std::string>& required_secrets) override;

    void setSecretsDirectory(const std::string& dir);

private:
    std::string secrets_dir_;
    mutable std::unordered_map<std::string, std::pair<std::string, SecretMetadata>> cache_;
    mutable std::mutex mutex_;

    std::string readSecretFile(const std::string& name) const;
};

/**
 * 组合密钥管理器（尝试多个来源）
 */
class CompositeSecretManager : public ISecretManager {
public:
    /**
     * 添加密钥来源
     * @param source 密钥管理器
     * @param priority 优先级（数字越小优先级越高）
     */
    void addSource(std::shared_ptr<ISecretManager> source, int priority = 0);

    std::optional<std::string> getSecret(const std::string& name) override;
    std::optional<std::pair<std::string, SecretMetadata>>
        getSecretWithMetadata(const std::string& name) override;
    bool reloadSecret(const std::string& name) override;
    void reloadAll() override;
    bool hasSecret(const std::string& name) const override;
    bool validateRequiredSecrets(
        const std::vector<std::string>& required_secrets) override;

private:
    std::vector<std::pair<int, std::shared_ptr<ISecretManager>>> sources_;
    mutable std::mutex mutex_;
};

/**
 * 全局密钥管理器访问器
 */
class SecretManager {
public:
    /**
     * 初始化全局密钥管理器
     */
    static void initialize(std::shared_ptr<ISecretManager> manager);

    /**
     * 获取全局密钥管理器实例
     */
    static ISecretManager& getInstance();

    /**
     * 检查是否已初始化
     */
    static bool isInitialized();

    /**
     * 获取密钥，不存在则抛出异常
     */
    static std::string getOrThrow(const std::string& name);

    /**
     * 获取密钥，不存在则返回默认值
     */
    static std::string getOrDefault(
        const std::string& name,
        const std::string& default_value);

    /**
     * 创建默认的组合密钥管理器
     * 优先级：环境变量 > 文件 > 硬编码（仅开发环境）
     */
    static std::shared_ptr<ISecretManager> createDefault();

private:
    static std::shared_ptr<ISecretManager> instance_;
    static std::mutex instance_mutex_;
};

/**
 * 应用程序必需的密钥列表
 */
inline const std::vector<std::string> REQUIRED_SECRETS = {
    "JWT_SECRET_KEY",
    "SESSION_ENCRYPTION_KEY",
    "MYSQL_PASSWORD",
    "REDIS_PASSWORD"
};

/**
 * 可选但推荐的密钥列表
 */
inline const std::vector<std::string> RECOMMENDED_SECRETS = {
    "API_GATEWAY_KEY",
    "KAFKA_SASL_PASSWORD",
    "ENCRYPTION_MASTER_KEY"
};

/**
 * 密钥验证异常
 */
class SecretValidationException : public std::runtime_error {
public:
    explicit SecretValidationException(const std::string& message)
        : std::runtime_error(message) {}

    static SecretValidationException missingSecret(const std::string& name) {
        return SecretValidationException(
            "Required secret '" + name + "' is not configured");
    }

    static SecretValidationException emptySecret(const std::string& name) {
        return SecretValidationException(
            "Required secret '" + name + "' is empty");
    }

    static SecretValidationException shortSecret(
        const std::string& name, size_t min_length) {
        return SecretValidationException(
            "Secret '" + name + "' must be at least " +
            std::to_string(min_length) + " characters long");
    }
};

/**
 * 密钥验证器
 */
class SecretValidator {
public:
    /**
     * 验证 JWT 密钥（至少 32 字符）
     */
    static bool validateJwtSecret(const std::string& secret);

    /**
     * 验证会话加密密钥（至少 32 字符）
     */
    static bool validateSessionEncryptionKey(const std::string& key);

    /**
     * 验证数据库密码（至少 8 字符）
     */
    static bool validateDatabasePassword(const std::string& password);

    /**
     * 验证所有必需密钥
     * @throws SecretValidationException 如果验证失败
     */
    static void validateAllRequired();
};

} // namespace security
} // namespace common
