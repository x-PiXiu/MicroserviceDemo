#pragma once

#include "common/database/redis_pool.h"
#include "common/logger/logger.h"
#include <string>
#include <chrono>
#include <atomic>
#include <random>
#include <thread>

namespace common {
namespace session {

/**
 * 分布式锁实现（基于 Redis）
 *
 * 特性：
 * - 自动过期防止死锁
 * - 唯一标识防止误删
 * - 可重入支持
 * - 看门狗自动续期
 */
class DistributedLock {
public:
    /**
     * 构造函数
     * @param redis_pool Redis 连接池
     * @param lock_name 锁名称
     * @param ttl 锁的过期时间
     */
    DistributedLock(
        std::shared_ptr<database::RedisPool> redis_pool,
        const std::string& lock_name,
        std::chrono::milliseconds ttl = std::chrono::milliseconds(30000)
    );

    ~DistributedLock();

    // 禁止拷贝
    DistributedLock(const DistributedLock&) = delete;
    DistributedLock& operator=(const DistributedLock&) = delete;

    // 允许移动
    DistributedLock(DistributedLock&&) noexcept;
    DistributedLock& operator=(DistributedLock&&) noexcept;

    /**
     * 尝试获取锁
     * @param timeout 等待超时时间（0 表示不等待）
     * @return 是否成功获取锁
     */
    bool tryAcquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    /**
     * 释放锁
     * @return 是否成功释放
     */
    bool release();

    /**
     * 检查锁是否被持有
     */
    bool isHeld() const { return held_.load(); }

    /**
     * 获取锁名称
     */
    const std::string& getLockName() const { return lock_name_; }

    /**
     * 开启看门狗（自动续期）
     * @param interval 续期间隔（应小于 TTL 的一半）
     */
    void startWatchdog(std::chrono::milliseconds interval = std::chrono::milliseconds(10000));

    /**
     * 停止看门狗
     */
    void stopWatchdog();

    /**
     * RAII 守卫
     */
    class Guard {
    public:
        explicit Guard(DistributedLock& lock);
        ~Guard();

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        Guard(Guard&&) noexcept;
        Guard& operator=(Guard&&) noexcept;

        bool isLocked() const { return owned_; }

    private:
        DistributedLock* lock_;
        bool owned_;
    };

private:
    std::shared_ptr<database::RedisPool> redis_pool_;
    std::string lock_name_;
    std::string lock_value_;  // 唯一标识，防止误删
    std::chrono::milliseconds ttl_;
    std::atomic<bool> held_{false};

    // 看门狗
    std::atomic<bool> watchdog_running_{false};
    std::thread watchdog_thread_;

    /**
     * 生成唯一的锁值
     */
    std::string generateLockValue();

    /**
     * Lua 脚本：原子性释放锁
     */
    static const char* RELEASE_SCRIPT;

    /**
     * Lua 脚本：原子性续期
     */
    static const char* RENEW_SCRIPT;
};

/**
 * 分布式锁工厂
 */
class DistributedLockFactory {
public:
    explicit DistributedLockFactory(
        std::shared_ptr<database::RedisPool> redis_pool,
        std::chrono::milliseconds default_ttl = std::chrono::milliseconds(30000)
    );

    /**
     * 创建分布式锁
     */
    std::unique_ptr<DistributedLock> createLock(const std::string& name);

    /**
     * 创建并获取锁（阻塞）
     */
    std::unique_ptr<DistributedLock> acquireLock(
        const std::string& name,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
    );

    /**
     * 创建并尝试获取锁（非阻塞）
     */
    std::unique_ptr<DistributedLock> tryAcquireLock(const std::string& name);

private:
    std::shared_ptr<database::RedisPool> redis_pool_;
    std::chrono::milliseconds default_ttl_;
};

} // namespace session
} // namespace common
