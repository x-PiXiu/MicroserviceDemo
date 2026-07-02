#include "common/security/secret_manager.h"
#include "common/logger/logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace common {
namespace security {

// ============================================================================
// EnvironmentSecretManager Implementation
// ============================================================================

EnvironmentSecretManager::EnvironmentSecretManager(
    const std::unordered_map<std::string, std::string>& env_mappings)
    : env_mappings_(env_mappings) {

    // 默认映射
    if (env_mappings_.empty()) {
        env_mappings_ = {
            {"JWT_SECRET_KEY", "JWT_SECRET_KEY"},
            {"SESSION_ENCRYPTION_KEY", "SESSION_ENCRYPTION_KEY"},
            {"MYSQL_PASSWORD", "MYSQL_PASSWORD"},
            {"REDIS_PASSWORD", "REDIS_PASSWORD"},
            {"API_GATEWAY_KEY", "API_GATEWAY_KEY"},
            {"KAFKA_SASL_PASSWORD", "KAFKA_SASL_PASSWORD"},
            {"ENCRYPTION_MASTER_KEY", "ENCRYPTION_MASTER_KEY"},

            // 简化别名
            {"jwt_secret", "JWT_SECRET_KEY"},
            {"session_key", "SESSION_ENCRYPTION_KEY"},
            {"mysql_password", "MYSQL_PASSWORD"},
            {"redis_password", "REDIS_PASSWORD"},
            {"api_key", "API_GATEWAY_KEY"}
        };
    }
}

std::string EnvironmentSecretManager::resolveEnvName(const std::string& logical_name) const {
    auto it = env_mappings_.find(logical_name);
    if (it != env_mappings_.end()) {
        return it->second;
    }
    // 如果没有映射，直接使用逻辑名称作为环境变量名
    return logical_name;
}

std::string EnvironmentSecretManager::getEnvVar(const std::string& name) const {
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : std::string();
}

std::optional<std::string> EnvironmentSecretManager::getSecret(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查缓存
    auto cache_it = cache_.find(name);
    if (cache_it != cache_.end()) {
        return cache_it->second.first;
    }

    // 从环境变量读取
    std::string env_name = resolveEnvName(name);
    std::string value = getEnvVar(env_name);

    if (value.empty()) {
        return std::nullopt;
    }

    // 缓存结果
    SecretMetadata metadata;
    metadata.name = name;
    metadata.source = SecretSource::EnvironmentVariable;
    metadata.loaded_at = std::chrono::system_clock::now();

    cache_[name] = {value, metadata};

    LOG_DEBUG("Loaded secret '" + name + "' from environment variable");
    return value;
}

std::optional<std::pair<std::string, SecretMetadata>>
EnvironmentSecretManager::getSecretWithMetadata(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查缓存
    auto cache_it = cache_.find(name);
    if (cache_it != cache_.end()) {
        return cache_it->second;
    }

    // 从环境变量读取
    std::string env_name = resolveEnvName(name);
    std::string value = getEnvVar(env_name);

    if (value.empty()) {
        return std::nullopt;
    }

    // 缓存结果
    SecretMetadata metadata;
    metadata.name = name;
    metadata.source = SecretSource::EnvironmentVariable;
    metadata.loaded_at = std::chrono::system_clock::now();

    cache_[name] = {value, metadata};

    return cache_[name];
}

bool EnvironmentSecretManager::reloadSecret(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 清除缓存
    cache_.erase(name);

    // 重新加载
    std::string env_name = resolveEnvName(name);
    std::string value = getEnvVar(env_name);

    if (value.empty()) {
        return false;
    }

    SecretMetadata metadata;
    metadata.name = name;
    metadata.source = SecretSource::EnvironmentVariable;
    metadata.loaded_at = std::chrono::system_clock::now();

    cache_[name] = {value, metadata};

    LOG_INFO("Reloaded secret '" + name + "'");
    return true;
}

void EnvironmentSecretManager::reloadAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    // 获取所有缓存的密钥名称
    std::vector<std::string> names;
    for (const auto& pair : cache_) {
        names.push_back(pair.first);
    }

    // 清除缓存
    cache_.clear();

    // 重新加载
    for (const auto& name : names) {
        std::string env_name = resolveEnvName(name);
        std::string value = getEnvVar(env_name);

        if (!value.empty()) {
            SecretMetadata metadata;
            metadata.name = name;
            metadata.source = SecretSource::EnvironmentVariable;
            metadata.loaded_at = std::chrono::system_clock::now();

            cache_[name] = {value, metadata};
        }
    }

    LOG_INFO("Reloaded all secrets from environment variables");
}

bool EnvironmentSecretManager::hasSecret(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查缓存
    if (cache_.find(name) != cache_.end()) {
        return true;
    }

    // 检查环境变量
    std::string env_name = resolveEnvName(name);
    std::string value = getEnvVar(env_name);

    return !value.empty();
}

bool EnvironmentSecretManager::validateRequiredSecrets(
    const std::vector<std::string>& required_secrets) {

    bool all_valid = true;

    for (const auto& name : required_secrets) {
        auto secret = getSecret(name);
        if (!secret.has_value() || secret->empty()) {
            LOG_ERROR("Required secret '" + name + "' is not configured or empty");
            all_valid = false;
        }
    }

    return all_valid;
}

void EnvironmentSecretManager::addMapping(
    const std::string& logical_name,
    const std::string& env_var) {

    std::lock_guard<std::mutex> lock(mutex_);
    env_mappings_[logical_name] = env_var;
}

// ============================================================================
// FileSecretManager Implementation
// ============================================================================

FileSecretManager::FileSecretManager(const std::string& secrets_dir)
    : secrets_dir_(secrets_dir) {
}

void FileSecretManager::setSecretsDirectory(const std::string& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    secrets_dir_ = dir;
    cache_.clear();  // 清除缓存，因为目录变了
}

std::string FileSecretManager::readSecretFile(const std::string& name) const {
    std::string file_path = secrets_dir_ + "/" + name;

    std::ifstream file(file_path);
    if (!file.is_open()) {
        return std::string();
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // 移除尾部空白（Docker Secrets 通常有换行符）
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) {
        content.pop_back();
    }

    return content;
}

std::optional<std::string> FileSecretManager::getSecret(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查缓存
    auto cache_it = cache_.find(name);
    if (cache_it != cache_.end()) {
        return cache_it->second.first;
    }

    // 从文件读取
    std::string value = readSecretFile(name);

    if (value.empty()) {
        return std::nullopt;
    }

    // 缓存结果
    SecretMetadata metadata;
    metadata.name = name;
    metadata.source = SecretSource::File;
    metadata.loaded_at = std::chrono::system_clock::now();

    cache_[name] = {value, metadata};

    LOG_DEBUG("Loaded secret '" + name + "' from file");
    return value;
}

std::optional<std::pair<std::string, SecretMetadata>>
FileSecretManager::getSecretWithMetadata(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查缓存
    auto cache_it = cache_.find(name);
    if (cache_it != cache_.end()) {
        return cache_it->second;
    }

    // 从文件读取
    std::string value = readSecretFile(name);

    if (value.empty()) {
        return std::nullopt;
    }

    // 缓存结果
    SecretMetadata metadata;
    metadata.name = name;
    metadata.source = SecretSource::File;
    metadata.loaded_at = std::chrono::system_clock::now();

    cache_[name] = {value, metadata};

    return cache_[name];
}

bool FileSecretManager::reloadSecret(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 清除缓存
    cache_.erase(name);

    // 重新加载
    std::string value = readSecretFile(name);

    if (value.empty()) {
        return false;
    }

    SecretMetadata metadata;
    metadata.name = name;
    metadata.source = SecretSource::File;
    metadata.loaded_at = std::chrono::system_clock::now();

    cache_[name] = {value, metadata};

    LOG_INFO("Reloaded secret '" + name + "' from file");
    return true;
}

void FileSecretManager::reloadAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    // 获取所有缓存的密钥名称
    std::vector<std::string> names;
    for (const auto& pair : cache_) {
        names.push_back(pair.first);
    }

    // 清除缓存
    cache_.clear();

    // 重新加载
    for (const auto& name : names) {
        std::string value = readSecretFile(name);

        if (!value.empty()) {
            SecretMetadata metadata;
            metadata.name = name;
            metadata.source = SecretSource::File;
            metadata.loaded_at = std::chrono::system_clock::now();

            cache_[name] = {value, metadata};
        }
    }

    LOG_INFO("Reloaded all secrets from files");
}

bool FileSecretManager::hasSecret(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查缓存
    if (cache_.find(name) != cache_.end()) {
        return true;
    }

    // 检查文件是否存在
    std::string file_path = secrets_dir_ + "/" + name;
    return std::filesystem::exists(file_path);
}

bool FileSecretManager::validateRequiredSecrets(
    const std::vector<std::string>& required_secrets) {

    bool all_valid = true;

    for (const auto& name : required_secrets) {
        auto secret = getSecret(name);
        if (!secret.has_value() || secret->empty()) {
            LOG_ERROR("Required secret '" + name + "' is not configured or empty");
            all_valid = false;
        }
    }

    return all_valid;
}

// ============================================================================
// CompositeSecretManager Implementation
// ============================================================================

void CompositeSecretManager::addSource(
    std::shared_ptr<ISecretManager> source,
    int priority) {

    std::lock_guard<std::mutex> lock(mutex_);

    sources_.emplace_back(priority, source);

    // 按优先级排序（数字越小优先级越高）
    std::sort(sources_.begin(), sources_.end(),
        [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
}

std::optional<std::string> CompositeSecretManager::getSecret(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [priority, source] : sources_) {
        auto secret = source->getSecret(name);
        if (secret.has_value()) {
            return secret;
        }
    }

    return std::nullopt;
}

std::optional<std::pair<std::string, SecretMetadata>>
CompositeSecretManager::getSecretWithMetadata(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [priority, source] : sources_) {
        auto secret = source->getSecretWithMetadata(name);
        if (secret.has_value()) {
            return secret;
        }
    }

    return std::nullopt;
}

bool CompositeSecretManager::reloadSecret(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    bool any_reloaded = false;
    for (const auto& [priority, source] : sources_) {
        if (source->reloadSecret(name)) {
            any_reloaded = true;
        }
    }

    return any_reloaded;
}

void CompositeSecretManager::reloadAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [priority, source] : sources_) {
        source->reloadAll();
    }
}

bool CompositeSecretManager::hasSecret(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [priority, source] : sources_) {
        if (source->hasSecret(name)) {
            return true;
        }
    }

    return false;
}

bool CompositeSecretManager::validateRequiredSecrets(
    const std::vector<std::string>& required_secrets) {

    bool all_valid = true;

    for (const auto& name : required_secrets) {
        if (!hasSecret(name)) {
            LOG_ERROR("Required secret '" + name + "' is not configured in any source");
            all_valid = false;
        }
    }

    return all_valid;
}

// ============================================================================
// SecretManager (Global Accessor) Implementation
// ============================================================================

std::shared_ptr<ISecretManager> SecretManager::instance_;
std::mutex SecretManager::instance_mutex_;

void SecretManager::initialize(std::shared_ptr<ISecretManager> manager) {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    instance_ = manager;
    LOG_INFO("SecretManager initialized");
}

ISecretManager& SecretManager::getInstance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);

    if (!instance_) {
        // 自动创建默认管理器
        instance_ = createDefault();
        LOG_INFO("SecretManager auto-initialized with default configuration");
    }

    return *instance_;
}

bool SecretManager::isInitialized() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    return instance_ != nullptr;
}

std::string SecretManager::getOrThrow(const std::string& name) {
    auto secret = getInstance().getSecret(name);

    if (!secret.has_value()) {
        throw SecretValidationException::missingSecret(name);
    }

    return secret.value();
}

std::string SecretManager::getOrDefault(
    const std::string& name,
    const std::string& default_value) {

    auto secret = getInstance().getSecret(name);
    return secret.value_or(default_value);
}

std::shared_ptr<ISecretManager> SecretManager::createDefault() {
    auto composite = std::make_shared<CompositeSecretManager>();

    // 优先级 0: 环境变量（最高优先级）
    composite->addSource(
        std::make_shared<EnvironmentSecretManager>(),
        0);

    // 优先级 10: 文件（Docker/K8s Secrets）
    composite->addSource(
        std::make_shared<FileSecretManager>(),
        10);

    return composite;
}

// ============================================================================
// SecretValidator Implementation
// ============================================================================

bool SecretValidator::validateJwtSecret(const std::string& secret) {
    return secret.length() >= 32;
}

bool SecretValidator::validateSessionEncryptionKey(const std::string& key) {
    return key.length() >= 32;
}

bool SecretValidator::validateDatabasePassword(const std::string& password) {
    return password.length() >= 8;
}

void SecretValidator::validateAllRequired() {
    auto& manager = SecretManager::getInstance();

    // 验证 JWT 密钥
    auto jwt_secret = manager.getSecret("JWT_SECRET_KEY");
    if (!jwt_secret.has_value() || jwt_secret->empty()) {
        throw SecretValidationException::missingSecret("JWT_SECRET_KEY");
    }
    if (!validateJwtSecret(*jwt_secret)) {
        throw SecretValidationException::shortSecret("JWT_SECRET_KEY", 32);
    }

    // 验证会话加密密钥
    auto session_key = manager.getSecret("SESSION_ENCRYPTION_KEY");
    if (!session_key.has_value() || session_key->empty()) {
        throw SecretValidationException::missingSecret("SESSION_ENCRYPTION_KEY");
    }
    if (!validateSessionEncryptionKey(*session_key)) {
        throw SecretValidationException::shortSecret("SESSION_ENCRYPTION_KEY", 32);
    }

    // 验证数据库密码
    auto mysql_password = manager.getSecret("MYSQL_PASSWORD");
    if (!mysql_password.has_value() || mysql_password->empty()) {
        throw SecretValidationException::missingSecret("MYSQL_PASSWORD");
    }

    auto redis_password = manager.getSecret("REDIS_PASSWORD");
    if (!redis_password.has_value() || redis_password->empty()) {
        throw SecretValidationException::missingSecret("REDIS_PASSWORD");
    }

    LOG_INFO("All required secrets validated successfully");
}

} // namespace security
} // namespace common
