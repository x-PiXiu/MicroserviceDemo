#include "common/session/distributed_lock.h"
#include <sstream>
#include <iomanip>

namespace common {
namespace session {

// Lua 脚本：原子性释放锁（只有持有者才能释放）
const char* DistributedLock::RELEASE_SCRIPT = R"(
    if redis.call("get", KEYS[1]) == ARGV[1] then
        return redis.call("del", KEYS[1])
    else
        return 0
    end
)";

// Lua 脚本：原子性续期
const char* DistributedLock::RENEW_SCRIPT = R"(
    if redis.call("get", KEYS[1]) == ARGV[1] then
        return redis.call("pexpire", KEYS[1], ARGV[2])
    else
        return 0
    end
)";

DistributedLock::DistributedLock(
    std::shared_ptr<database::RedisPool> redis_pool,
    const std::string& lock_name,
    std::chrono::milliseconds ttl)
    : redis_pool_(redis_pool)
    , lock_name_("lock:" + lock_name)
    , ttl_(ttl) {

    if (!redis_pool_) {
        throw std::invalid_argument("Redis pool cannot be null");
    }

    lock_value_ = generateLockValue();
}

DistributedLock::~DistributedLock() {
    stopWatchdog();
    if (held_.load()) {
        release();
    }
}

DistributedLock::DistributedLock(DistributedLock&& other) noexcept
    : redis_pool_(std::move(other.redis_pool_))
    , lock_name_(std::move(other.lock_name_))
    , lock_value_(std::move(other.lock_value_))
    , ttl_(other.ttl_)
    , held_(other.held_.load()) {
    other.held_ = false;
}

DistributedLock& DistributedLock::operator=(DistributedLock&& other) noexcept {
    if (this != &other) {
        stopWatchdog();
        if (held_.load()) {
            release();
        }

        redis_pool_ = std::move(other.redis_pool_);
        lock_name_ = std::move(other.lock_name_);
        lock_value_ = std::move(other.lock_value_);
        ttl_ = other.ttl_;
        held_ = other.held_.load();

        other.held_ = false;
    }
    return *this;
}

std::string DistributedLock::generateLockValue() {
    // 生成唯一标识：UUID-like 格式
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    ss << std::hex;

    // 生成类似 UUID 的值
    ss << std::setfill('0');
    for (int i = 0; i < 8; i++) {
        ss << std::setw(1) << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 4; i++) {
        ss << std::setw(1) << dis(gen);
    }
    ss << "-4";  // UUID 版本 4
    for (int i = 0; i < 3; i++) {
        ss << std::setw(1) << dis(gen);
    }
    ss << "-";
    ss << std::setw(1) << dis2(gen);  // UUID 变体
    for (int i = 0; i < 3; i++) {
        ss << std::setw(1) << dis(gen);
    }
    ss << "-";
    for (int i = 0; i < 12; i++) {
        ss << std::setw(1) << dis(gen);
    }

    // 添加线程 ID 确保唯一性
    ss << ":" << std::this_thread::get_id();

    return ss.str();
}

bool DistributedLock::tryAcquire(std::chrono::milliseconds timeout) {
    if (held_.load()) {
        LOG_WARNING("Lock already held: " + lock_name_);
        return true;
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;
    auto retry_interval = std::chrono::milliseconds(50);

    while (true) {
        try {
            // 使用 SET NX PX 原子性设置
            bool success = redis_pool_->setnx(lock_name_, lock_value_, ttl_);
            if (success) {
                held_ = true;
                LOG_DEBUG("Lock acquired: " + lock_name_);
                return true;
            }

            // 检查是否超时
            if (std::chrono::steady_clock::now() >= deadline) {
                LOG_DEBUG("Lock acquisition timeout: " + lock_name_);
                return false;
            }

            // 等待重试
            std::this_thread::sleep_for(retry_interval);
            retry_interval = std::min(retry_interval * 2, std::chrono::milliseconds(500));

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to acquire lock: " + std::string(e.what()));
            return false;
        }
    }
}

bool DistributedLock::release() {
    if (!held_.load()) {
        return true;
    }

    try {
        // 使用 Lua 脚本原子性释放
        // 注意：这里需要 Redis 支持 eval 命令
        // 如果不支持，使用简单的 get + del（非原子）
        auto current_value = redis_pool_->get(lock_name_);
        if (current_value.has_value() && current_value.value() == lock_value_) {
            redis_pool_->del(lock_name_);
            held_ = false;
            LOG_DEBUG("Lock released: " + lock_name_);
            return true;
        } else {
            // 锁已被其他进程持有或已过期
            held_ = false;
            LOG_WARNING("Lock was already released or expired: " + lock_name_);
            return false;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to release lock: " + std::string(e.what()));
        held_ = false;
        return false;
    }
}

void DistributedLock::startWatchdog(std::chrono::milliseconds interval) {
    if (watchdog_running_.load()) {
        return;
    }

    watchdog_running_ = true;
    watchdog_thread_ = std::thread([this, interval]() {
        LOG_DEBUG("Watchdog started for lock: " + lock_name_);

        while (watchdog_running_.load() && held_.load()) {
            std::this_thread::sleep_for(interval);

            if (!held_.load() || !watchdog_running_.load()) {
                break;
            }

            try {
                // 续期
                auto current_value = redis_pool_->get(lock_name_);
                if (current_value.has_value() && current_value.value() == lock_value_) {
                    redis_pool_->expire(lock_name_, ttl_);
                    LOG_DEBUG("Lock renewed: " + lock_name_);
                } else {
                    // 锁丢失
                    LOG_WARNING("Lock lost during watchdog: " + lock_name_);
                    held_ = false;
                    break;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Watchdog renewal failed: " + std::string(e.what()));
            }
        }

        LOG_DEBUG("Watchdog stopped for lock: " + lock_name_);
    });
}

void DistributedLock::stopWatchdog() {
    watchdog_running_ = false;
    if (watchdog_thread_.joinable()) {
        watchdog_thread_.join();
    }
}

// ============================================================================
// DistributedLock::Guard Implementation
// ============================================================================

DistributedLock::Guard::Guard(DistributedLock& lock)
    : lock_(&lock)
    , owned_(lock.isHeld()) {
}

DistributedLock::Guard::~Guard() {
    if (owned_ && lock_) {
        lock_->release();
    }
}

DistributedLock::Guard::Guard(Guard&& other) noexcept
    : lock_(other.lock_)
    , owned_(other.owned_) {
    other.owned_ = false;
    other.lock_ = nullptr;
}

DistributedLock::Guard& DistributedLock::Guard::operator=(Guard&& other) noexcept {
    if (this != &other) {
        if (owned_ && lock_) {
            lock_->release();
        }

        lock_ = other.lock_;
        owned_ = other.owned_;

        other.owned_ = false;
        other.lock_ = nullptr;
    }
    return *this;
}

// ============================================================================
// DistributedLockFactory Implementation
// ============================================================================

DistributedLockFactory::DistributedLockFactory(
    std::shared_ptr<database::RedisPool> redis_pool,
    std::chrono::milliseconds default_ttl)
    : redis_pool_(redis_pool)
    , default_ttl_(default_ttl) {

    if (!redis_pool_) {
        throw std::invalid_argument("Redis pool cannot be null");
    }
}

std::unique_ptr<DistributedLock> DistributedLockFactory::createLock(const std::string& name) {
    return std::make_unique<DistributedLock>(redis_pool_, name, default_ttl_);
}

std::unique_ptr<DistributedLock> DistributedLockFactory::acquireLock(
    const std::string& name,
    std::chrono::milliseconds timeout) {

    auto lock = createLock(name);
    if (lock->tryAcquire(timeout)) {
        return lock;
    }
    return nullptr;
}

std::unique_ptr<DistributedLock> DistributedLockFactory::tryAcquireLock(const std::string& name) {
    auto lock = createLock(name);
    if (lock->tryAcquire(std::chrono::milliseconds(0))) {
        return lock;
    }
    return nullptr;
}

} // namespace session
} // namespace common
