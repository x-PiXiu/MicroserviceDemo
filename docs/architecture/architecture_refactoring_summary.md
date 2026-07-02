# 微服务架构重构总结

## 概述

本次重构引入了统一的 Repository 模式，包括 MySQL Repository 和 Redis Repository，以解决现有代码中数据访问层分散、安全性问题、并发问题等。

---

## 已解决的问题

### 1. 安全问题

| 问题 | 解决方案 | 状态 |
|------|----------|------|
| JWT 密钥硬编码 | 引入 SecretManager，从环境变量/文件读取 | ✅ 已修复 |
| Token 验证绕过 | 移除 test_token 后门，使用真实 JWT 验证 | ✅ 已修复 |
| SQL 注入风险 | 引入 QueryBuilder，自动参数化查询 | ✅ 已修复 |

### 2. 并发问题

| 问题 | 解决方案 | 状态 |
|------|----------|------|
| ThreadPool detach() 资源泄漏 | 使用 join() 替代 detach() | ✅ 已修复 |
| CircuitBreaker atomic<time_point> | 使用 atomic<int64_t> 存储纳秒 | ✅ 已修复 |
| MySQLPool 死锁风险 | 使用 "reservation slot" 模式 | ✅ 已修复 |

### 3. 架构问题

| 问题 | 解决方案 | 状态 |
|------|----------|------|
| Redis 操作分散 | 引入 IRedisRepository + BaseRedisRepository | ✅ 已完成 |
| 键命名不规范 | 引入 RedisKeyBuilder | ✅ 已完成 |
| TTL 硬编码 | 引入 RedisTTLConfig | ✅ 已完成 |
| 无统一仓储模式 | 引入 IRepository + BaseRepository | ✅ 已完成 |

---

## 新架构组件

### 通用组件 (`include/common/`)

#### 安全模块 (`security/`)
```
secret_manager.h          # 密钥管理抽象层
```

#### 仓储模块 (`repository/`)
```
repository_error.h        # 错误类型定义（使用 std::expected）
i_repository.h            # MySQL 仓储接口
base_repository.h         # MySQL 仓储基类
query_builder.h           # 安全 SQL 构建器
unit_of_work.h            # 事务管理
cache_manager.h           # 双层缓存管理

redis_key_builder.h       # Redis 键名构建器
redis_ttl_config.h        # Redis TTL 集中配置
i_redis_repository.h      # Redis 仓储接口
base_redis_repository.h   # Redis 仓储基类
```

#### 会话模块 (`session/`)
```
distributed_lock.h        # Redis 分布式锁
distributed_session_manager.h  # 分布式会话管理
```

### 服务专用组件

#### User Service (`src/core_services/user_service/`)
```
include/user_redis_repository.h
├── UserRedisRepository          # 用户信息缓存
├── UserProfileRedisRepository   # 用户档案缓存
├── UserPreferencesRedisRepository # 用户偏好缓存
└── UserSessionRedisRepository   # 用户会话缓存
```

#### Auth Service (`src/core_services/auth_service/`)
```
include/session_redis_repository.h
├── SessionRedisRepository       # 会话管理
├── DeviceRedisRepository        # 设备信息管理
└── SecurityEventRedisRepository # 安全事件记录
```

#### Gomoku Service (`src/game_services/gomoku/`)
```
include/gomoku_redis_repository.h
├── UserStatsRedisRepository     # 用户统计
├── GameRecordRedisRepository    # 游戏记录
├── LeaderboardRedisRepository   # 排行榜（ZSET）
└── RoomStateRedisRepository     # 房间状态
```

#### Game Data Service (`src/core_services/game_data_service/`)
```
include/game_data_redis_repository.h
├── UserGameProfileRedisRepository   # 用户游戏档案缓存
├── UserCurrencyRedisRepository      # 用户货币缓存（含原子增减）
├── UserInventoryRedisRepository     # 用户库存缓存（含类型索引）
└── GameAchievementRedisRepository   # 游戏成就缓存
```

---

## 设计模式

### 1. Repository 模式

```
┌─────────────┐     ┌─────────────────┐     ┌─────────────┐
│   Service   │────▶│   Repository    │────▶│  Database   │
└─────────────┘     └────────┬────────┘     └─────────────┘
                             │
                    ┌────────┴────────┐
                    │                 │
              ┌─────▼─────┐     ┌─────▼─────┐
              │   MySQL   │     │   Redis   │
              │ Repository│     │ Repository│
              └───────────┘     └───────────┘
```

### 2. Cache-Aside 模式

```cpp
// 获取或加载（自动缓存填充）
auto result = user_redis_->getOrLoad(user_id, [&]() {
    return loadFromMySQL(user_id);  // 仅在缓存未命中时调用
});
```

### 3. Strategy 模式（SecretManager）

```cpp
// 支持多种密钥来源
auto secret = SecretManager::getSecret("JWT_SECRET_KEY");
// 来源优先级: 环境变量 > 文件 > Vault
```

---

## 键命名规范

### 通用格式

```
{service}:{entity}:{id}           # 实体
{service}:idx:{index}:{value}     # 二级索引
{service}:counter:{name}          # 计数器
{service}:lock:{resource}         # 分布式锁
session:{session_id}              # 会话（兼容旧格式）
```

### 示例

| 服务 | 实体 | 键名 |
|------|------|------|
| user | 用户 | `user:user:user_123` |
| user | 档案 | `user:profile:user_123` |
| auth | 会话 | `session:sess_abc` |
| auth | 设备 | `auth:device:user_123:device_456` |
| gomoku | 统计 | `gomoku:stats:user_123` |
| gomoku | 排行榜 | `gomoku:leaderboard:standard` |

---

## TTL 策略

### 分级标准

| 级别 | 时长 | 使用场景 |
|------|------|----------|
| VERY_SHORT | 10秒 | 高频变化数据（如等待玩家列表） |
| SHORT | 1分钟 | 实时性要求高（如排行榜） |
| MEDIUM | 5分钟 | 一般数据 |
| DEFAULT | 1小时 | 默认值 |
| LONG | 24小时 | 索引数据、设备信息 |
| VERY_LONG | 7天 | 安全事件日志 |

### 服务配置

```cpp
// 在 RedisTTLConfig 中定义
struct UserService {
    static constexpr std::chrono::seconds USER_INFO{3600};
    static constexpr std::chrono::seconds USERNAME_INDEX{86400};
};
```

---

## 向后兼容策略

### 1. 渐进式迁移

```cpp
class UserRepository {
private:
    // 新架构
    std::unique_ptr<UserRedisRepository> user_redis_;

    // 旧架构（向后兼容）
    std::shared_ptr<RedisPool> redis_pool_;

public:
    std::optional<UserInfo> getUserById(const std::string& user_id) {
        // 优先使用新架构
        if (user_redis_) {
            auto result = user_redis_->findById(user_id);
            if (result.has_value() && result->has_value()) {
                return result->value();
            }
        }
        // 降级到旧逻辑
        return loadFromMySQLWithOldCache(user_id);
    }
};
```

### 2. 键名兼容

```cpp
// 新键名: user:user:user_123
// 旧键名: user_service:info:user_123

// 读取时尝试两种格式
std::optional<T> findById(const std::string& id) {
    auto new_key = RedisKeyBuilder::UserService::user(id);
    auto old_key = "user_service:info:" + id;

    // 先尝试新格式
    auto value = conn->get(new_key);
    if (!value.empty()) return deserialize(value);

    // 回退到旧格式
    value = conn->get(old_key);
    if (!value.empty()) {
        // 迁移到新格式
        conn->set(new_key, value, ttl);
        return deserialize(value);
    }

    return std::nullopt;
}
```

---

## 测试策略

### 单元测试

```cpp
// 使用 Mock Redis 连接
class MockRedisConnection : public RedisConnection {
public:
    MOCK_METHOD(std::string, get, (const std::string& key), (override));
    MOCK_METHOD(bool, set, (const std::string& key, const std::string& value, int ttl), (override));
};

TEST(UserRedisRepositoryTest, SaveAndFind) {
    auto mock_pool = std::make_shared<MockRedisPool>();
    UserRedisRepository repo(mock_pool);

    UserInfo user{"user_123", "testuser", "test@example.com"};
    EXPECT_TRUE(repo.save("user_123", user));

    auto result = repo.findById("user_123");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->value().username, "testuser");
}
```

### 集成测试

```cpp
TEST(UserRepositoryIntegrationTest, CacheAside) {
    // 使用真实 Redis（测试环境）
    auto repo = createTestUserRepository();

    // 第一次查询 - 从数据库加载
    auto user1 = repo->getUserById("user_123");
    EXPECT_TRUE(user1.has_value());

    // 第二次查询 - 从缓存获取
    auto user2 = repo->getUserById("user_123");
    EXPECT_TRUE(user2.has_value());
    EXPECT_EQ(user1->user_id, user2->user_id);
}
```

---

## 后续工作

### 已完成任务

1. [x] 完成 Auth Service 重构（SessionManager）- 2025-02-17
2. [x] 完成 Gomoku Service 重构（GomokuDatabase）- 2025-02-17
3. [x] 添加 Game Data Service Redis Repository - 2025-02-17
4. [x] 检查并验证 User Service 新架构集成 - 2025-02-17
5. [x] 完成 Game Data Service 与新 Redis Repository 集成 - 2025-02-17
   - GameRepository 现已使用类型化的 Redis Repository
   - 实现了用户档案、货币、库存、成就的 Cache-Aside 模式
   - 添加了货币原子增减操作和库存类型索引
   - 保持了向后兼容性（降级到旧缓存机制）
6. [x] 完成 Auth Service 与新 Redis Repository 完整集成 - 2025-02-17
   - SessionRedisRepository 支持加密/解密回调和动态 TTL
   - 会话存储方法：storeSessionToRedis, getSessionFromRedis, removeSessionFromRedis
   - 用户会话列表：updateUserSessionsList
   - 设备管理方法：getUserDevices, registerDevice, setDeviceTrusted, removeDevice
   - 安全事件方法：recordSecurityEvent, getUserSecurityEvents
   - SecurityEventRedisRepository 已初始化
   - 保持了向后兼容性（降级机制）

### 服务集成状态

| 服务 | Redis Repository | 集成状态 | 说明 |
|------|------------------|----------|------|
| User Service | UserRedisRepository 等 | ✅ 完成 | getUserById, clearUserCache 已集成 |
| Auth Service | SessionRedisRepository 等 | ✅ 完成 | 所有方法已集成新架构，支持加密回调和动态 TTL |
| Gomoku Service | UserStatsRedisRepository 等 | ✅ 完成 | getUser 已集成 Cache-Aside 模式 |
| Game Data Service | UserGameProfileRedisRepository 等 | ✅ 完成 | 全部方法已集成新架构 |

### 待完成任务

1. [ ] 添加 Payment Service Redis Repository
2. [ ] 编写单元测试和集成测试
3. [ ] 性能基准测试

### 建议优化

1. **监控集成**: 添加 Prometheus 指标（缓存命中率、延迟）
2. **配置热更新**: 支持运行时修改 TTL 配置
3. **批量操作优化**: 使用 Redis Pipeline 减少往返
4. **压缩支持**: 对大对象启用压缩存储

---

## 文档索引

| 文档 | 路径 |
|------|------|
| Redis Repository 架构 | `docs/architecture/redis_repository_architecture.md` |
| 数据库设计 | `src/core_services/*/database/*.sql` |
| API 网关流程 | `docs/architecture/API_Gateway_Business_Flow.mmd` |
| 各服务业务流程 | `docs/architecture/*_Business_Flow.mmd` |

---

## 版本历史

| 版本 | 日期 | 变更内容 |
|------|------|----------|
| 2.2.0 | 2025-02-17 | 完成 Auth Service 与新 Redis Repository 完整集成 |
| 2.1.0 | 2025-02-17 | 完成 Game Data Service 与新 Redis Repository 集成 |
| 2.0.0 | 2025-02-17 | 引入统一 Repository 模式，修复安全和并发问题 |
| 1.0.0 | 2025-09-19 | 初始版本 |
