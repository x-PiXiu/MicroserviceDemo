# Redis Repository 架构设计文档

## 概述

本文档描述了微服务项目中 Redis Repository 模式的统一架构设计，旨在解决现有代码中 Redis 操作分散、键命名不规范、TTL 硬编码等问题。

## 设计目标

1. **统一抽象**：提供类型化的 Redis Repository 接口
2. **规范命名**：使用 `{service}:{entity}:{id}` 格式
3. **集中配置**：TTL 值统一管理
4. **易于扩展**：支持二级索引、批量操作、Cache-Aside 模式

---

## 架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        Service Layer                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │ UserService │  │ AuthService │  │    GomokuService        │ │
│  └──────┬──────┘  └──────┬──────┘  └───────────┬─────────────┘ │
└─────────┼────────────────┼─────────────────────┼───────────────┘
          │                │                     │
          ▼                ▼                     ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Redis Repository Layer                       │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    IRedisRepository<T>                       ││
│  │  - findById(id) → RedisResult<optional<T>>                   ││
│  │  - save(id, entity, ttl) → RedisResult<bool>                 ││
│  │  - remove(id) → RedisResult<bool>                            ││
│  │  - createIndex(name, value, id) → RedisResult<bool>          ││
│  │  - getOrLoad(id, loader) → RedisResult<T>                    ││
│  └─────────────────────────────────────────────────────────────┘│
│                              △                                   │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                 BaseRedisRepository<T>                       ││
│  │  - 实现 IRedisRepository 所有方法                             ││
│  │  - 提供 toJson/fromJson 抽象方法供子类实现                     ││
│  │  - 使用 RedisKeyBuilder 构建键名                              ││
│  │  - 使用 RedisTTLConfig 获取默认 TTL                           ││
│  └─────────────────────────────────────────────────────────────┘│
│          △                △                     △               │
│  ┌───────┴───────┐ ┌──────┴──────┐ ┌───────────┴────────────┐  │
│  │UserRedisRepo  │ │SessionRedis │ │GomokuRedisRepository   │  │
│  │ProfileRedis   │ │DeviceRedis  │ │UserStatsRedis          │  │
│  │PrefsRedis     │ │SecurityEvent│ │GameRecordRedis         │  │
│  └───────────────┘ └─────────────┘ │LeaderboardRedis        │  │
│                                     │RoomStateRedis          │  │
│                                     └────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Infrastructure Layer                         │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐ │
│  │ RedisKeyBuilder │  │ RedisTTLConfig  │  │   RedisPool     │ │
│  │ 统一键名构建     │  │ 集中TTL管理     │  │  连接池管理     │ │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

---

## 核心组件

### 1. RedisKeyBuilder - 统一键名管理

**文件**: `include/common/repository/redis_key_builder.h`

**键命名规范**:
```
{service}:{entity}:{id}           - 单个实体
{service}:{entity}:{id}:{field}   - 实体字段
{service}:list:{query_hash}       - 查询结果列表
{service}:counter:{name}          - 计数器
{service}:lock:{resource}         - 分布式锁
{service}:idx:{index_name}:{value} - 二级索引
```

**使用示例**:
```cpp
// 构建用户键
auto key = RedisKeyBuilder::UserService::user("user_123");
// 结果: "user:user:user_123"

// 构建会话键
auto session_key = RedisKeyBuilder::AuthService::session("sess_abc");
// 结果: "session:sess_abc"

// 构建排行榜键
auto lb_key = RedisKeyBuilder::GomokuService::leaderboard("standard");
// 结果: "gomoku:leaderboard:standard"
```

### 2. RedisTTLConfig - 集中 TTL 管理

**文件**: `include/common/repository/redis_ttl_config.h`

**TTL 分级**:
| 级别 | 时长 | 适用场景 |
|------|------|----------|
| VERY_SHORT | 10秒 | 高频变化数据 |
| SHORT | 1分钟 | 实时性要求高 |
| MEDIUM | 5分钟 | 一般数据 |
| DEFAULT | 1小时 | 默认值 |
| LONG | 24小时 | 索引数据 |
| VERY_LONG | 7天 | 历史数据 |

**服务专用 TTL**:
```cpp
namespace RedisTTLConfig {
    struct UserService {
        static constexpr std::chrono::seconds USER_INFO{3600};      // 1小时
        static constexpr std::chrono::seconds USERNAME_INDEX{86400}; // 24小时
    };

    struct AuthService {
        static constexpr std::chrono::seconds SESSION{3600};        // 1小时
        static constexpr std::chrono::seconds DEVICE_INFO{86400};   // 24小时
    };

    struct GomokuService {
        static constexpr std::chrono::seconds LEADERBOARD{300};     // 5分钟
        static constexpr std::chrono::seconds ROOM_STATE{7200};     // 2小时
    };
}
```

### 3. IRedisRepository - Redis 仓储接口

**文件**: `include/common/repository/i_redis_repository.h`

**核心方法**:
```cpp
template<typename T, typename IdType = std::string>
class IRedisRepository {
public:
    // 基础 CRUD
    virtual RedisResult<std::optional<T>> findById(const IdType& id) = 0;
    virtual RedisResult<bool> save(const IdType& id, const T& entity, std::chrono::seconds ttl = 0) = 0;
    virtual RedisResult<bool> remove(const IdType& id) = 0;
    virtual RedisResult<bool> exists(const IdType& id) = 0;

    // 批量操作
    virtual RedisResult<std::vector<std::pair<IdType, T>>> findByIds(const std::vector<IdType>& ids) = 0;
    virtual RedisResult<bool> saveAll(const std::vector<std::pair<IdType, T>>& entities, std::chrono::seconds ttl = 0) = 0;

    // 缓存操作
    virtual RedisResult<T> getOrLoad(const IdType& id, std::function<std::optional<T>()> loader, std::chrono::seconds ttl = 0) = 0;
    virtual RedisResult<bool> invalidate(const IdType& id) = 0;
    virtual RedisResult<bool> refreshTTL(const IdType& id, std::chrono::seconds ttl) = 0;

    // 二级索引
    virtual RedisResult<bool> createIndex(const std::string& index_name, const std::string& index_value, const IdType& id) = 0;
    virtual RedisResult<std::optional<IdType>> findByIndex(const std::string& index_name, const std::string& index_value) = 0;
};
```

### 4. BaseRedisRepository - 基类实现

**文件**: `include/common/repository/base_redis_repository.h`

**子类实现只需**:
```cpp
class UserRedisRepository : public BaseRedisRepository<UserInfo> {
public:
    UserRedisRepository(std::shared_ptr<RedisPool> pool)
        : BaseRedisRepository(pool, "user", "user", RedisTTLConfig::UserService::USER_INFO) {}

protected:
    nlohmann::json toJson(const UserInfo& user) const override {
        return user.toJson();  // 使用 model 的 toJson 方法
    }

    std::optional<UserInfo> fromJson(const nlohmann::json& json) const override {
        return UserInfo::fromJson(json);  // 使用 model 的 fromJson 方法
    }
};
```

---

## 服务集成

### User Service

**Repository 类**:
| 类 | 实体类型 | 键格式 | 默认 TTL |
|----|----------|--------|----------|
| `UserRedisRepository` | `UserInfo` | `user:user:{id}` | 1小时 |
| `UserProfileRedisRepository` | `UserProfile` | `user:profile:{id}` | 1小时 |
| `UserPreferencesRedisRepository` | `UserPreferences` | `user:prefs:{id}` | 2小时 |

**使用示例**:
```cpp
// 在 UserRepository 中集成
class UserRepository {
private:
    std::unique_ptr<UserRedisRepository> user_redis_;
    std::unique_ptr<UserProfileRedisRepository> profile_redis_;

public:
    std::optional<UserInfo> getUserById(const std::string& user_id) {
        // 使用新架构：Cache-Aside 模式
        if (user_redis_) {
            auto result = user_redis_->getOrLoad(user_id, [&]() {
                return loadUserFromMySQL(user_id);  // 从数据库加载
            });
            if (result.has_value()) {
                return result.value();
            }
        }
        // 降级到旧逻辑
        return loadUserFromMySQL(user_id);
    }
};
```

### Auth Service

**Repository 类**:
| 类 | 实体类型 | 键格式 | 默认 TTL |
|----|----------|--------|----------|
| `SessionRedisRepository` | `UserSession` | `session:{id}` | 1小时 |
| `DeviceRedisRepository` | `DeviceInfo` | `auth:device:{user_id}:{device_id}` | 24小时 |
| `SecurityEventRedisRepository` | `nlohmann::json` | `auth:security_events:{user_id}` | 7天 |

**特殊功能**:
- `SessionRedisRepository::setEncryptionCallbacks(encrypt, decrypt)` - 设置加密/解密回调
- `SessionRedisRepository::saveWithTTL(session_id, session, ttl)` - 支持动态 TTL
- `SessionRedisRepository::getUserSessions(user_id)` - 获取用户所有会话
- `SessionRedisRepository::isConcurrentLimitExceeded(user_id, max)` - 检查并发限制
- `SessionRedisRepository::addToUserSessions(user_id, session_id)` - 添加到用户会话列表
- `SessionRedisRepository::removeFromUserSessions(user_id, session_id)` - 从用户会话列表移除
- `DeviceRedisRepository::getUserDevices(user_id)` - 获取用户设备列表
- `DeviceRedisRepository::saveDevice(user_id, device)` - 保存设备
- `DeviceRedisRepository::setDeviceTrusted(user_id, device_id, trusted)` - 设置信任状态
- `DeviceRedisRepository::removeDevice(user_id, device_id)` - 移除设备
- `SecurityEventRedisRepository::recordEvent(user_id, event)` - 记录安全事件
- `SecurityEventRedisRepository::getEvents(user_id, limit)` - 获取安全事件

### Gomoku Service

**Repository 类**:
| 类 | 实体类型 | 键格式 | 默认 TTL |
|----|----------|--------|----------|
| `UserStatsRedisRepository` | `UserGameStats` | `gomoku:stats:{user_id}` | 30分钟 |
| `GameRecordRedisRepository` | `GameRecord` | `gomoku:record:{game_id}` | 1小时 |
| `LeaderboardRedisRepository` | ZSET | `gomoku:leaderboard:{mode}` | 5分钟 |
| `RoomStateRedisRepository` | `nlohmann::json` | `gomoku:room:{room_id}` | 2小时 |

**特殊功能**:
- `LeaderboardRedisRepository::updateScore(mode, user_id, score)` - 更新积分
- `LeaderboardRedisRepository::getRange(mode, start, stop)` - 获取排行榜范围
- `RoomStateRedisRepository::saveRoomState(room_id, state)` - 保存房间状态

### Game Data Service

**Repository 类**:
| 类 | 实体类型 | 键格式 | 默认 TTL |
|----|----------|--------|----------|
| `UserGameProfileRedisRepository` | `UserGameProfile` | `game:profile:{user_id}` | 30分钟 |
| `UserCurrencyRedisRepository` | `UserCurrency` | `game:currency:{user_id}:{type}` | 10分钟 |
| `UserInventoryRedisRepository` | `UserInventoryItem` | `game:inventory:{user_id}:{item_id}` | 30分钟 |
| `GameAchievementRedisRepository` | `GameAchievement` | `game:achievement:{user_id}:{achievement_id}` | 1小时 |

**特殊功能**:
- `UserCurrencyRedisRepository::getCurrency(user_id, currency_type)` - 获取指定类型货币
- `UserCurrencyRedisRepository::saveCurrency(currency)` - 保存货币
- `UserCurrencyRedisRepository::incrementCurrency(user_id, currency_type, amount)` - 原子增减货币
- `UserInventoryRedisRepository::getUserInventory(user_id)` - 获取用户所有库存
- `UserInventoryRedisRepository::saveItem(item)` - 保存库存物品（含类型索引）
- `UserInventoryRedisRepository::findByType(user_id, item_type)` - 按类型查找物品
- `GameAchievementRedisRepository::getUserAchievements(user_id)` - 获取用户所有成就

**使用示例**:
```cpp
// 在 GameRepository 中集成
class GameRepository {
private:
    std::unique_ptr<UserGameProfileRedisRepository> profile_redis_;
    std::unique_ptr<UserCurrencyRedisRepository> currency_redis_;
    std::unique_ptr<UserInventoryRedisRepository> inventory_redis_;
    std::unique_ptr<GameAchievementRedisRepository> achievement_redis_;

public:
    std::optional<UserGameProfile> getUserProfile(const std::string& user_id) {
        // 使用新架构：Cache-Aside 模式
        if (profile_redis_) {
            auto result = profile_redis_->getOrLoad(user_id, [&]() {
                return loadProfileFromMySQL(user_id);
            });
            if (result.has_value()) {
                return result.value();
            }
        }
        // 降级到旧逻辑
        return loadProfileFromMySQL(user_id);
    }

    bool addUserCurrency(const std::string& user_id, const std::string& currency_type, long amount) {
        // 使用原子操作
        if (currency_redis_) {
            auto result = currency_redis_->incrementCurrency(user_id, currency_type, amount);
            if (result.has_value()) {
                syncCurrencyToMySQL(user_id, currency_type);
                return true;
            }
        }
        return addCurrencyToMySQL(user_id, currency_type, amount);
    }
};
```

---

## 文件结构

```
include/common/repository/
├── redis_key_builder.h        # 键名构建器
├── redis_ttl_config.h         # TTL 配置
├── i_redis_repository.h       # Redis 仓储接口
└── base_redis_repository.h    # Redis 仓储基类

src/core_services/user_service/include/
└── user_redis_repository.h    # 用户服务 Redis Repository

src/core_services/auth_service/include/
└── session_redis_repository.h # 认证服务 Redis Repository

src/game_services/gomoku/include/
└── gomoku_redis_repository.h  # 五子棋服务 Redis Repository

src/core_services/game_data_service/include/
└── game_data_redis_repository.h  # 游戏数据服务 Redis Repository
```

---

## 迁移指南

### 1. 新增 Redis Repository

```cpp
// 1. 定义实体类（确保有 toJson/fromJson 方法）
struct MyEntity {
    std::string id;
    std::string name;

    nlohmann::json toJson() const;
    static MyEntity fromJson(const nlohmann::json& json);
};

// 2. 创建 Repository 类
class MyRedisRepository : public BaseRedisRepository<MyEntity> {
public:
    MyRedisRepository(std::shared_ptr<RedisPool> pool)
        : BaseRedisRepository(pool, "myservice", "myentity",
                              RedisTTLConfig::DEFAULT_TTL) {}

protected:
    nlohmann::json toJson(const MyEntity& entity) const override {
        return entity.toJson();
    }

    std::optional<MyEntity> fromJson(const nlohmann::json& json) const override {
        return MyEntity::fromJson(json);
    }
};

// 3. 在服务中使用
auto repo = std::make_unique<MyRedisRepository>(redis_pool);
repo->save("id123", entity);
auto loaded = repo->findById("id123");
```

### 2. 替换现有 Redis 操作

**旧代码**:
```cpp
auto conn = redis_pool_->getConnection();
std::string key = "user:info:" + user_id;
conn->set(key, user.toJson().dump(), 3600);
```

**新代码**:
```cpp
user_redis_->save(user_id, user);  // TTL 自动使用默认值
```

---

## 最佳实践

1. **优先使用新架构**: 新功能应使用 `BaseRedisRepository`
2. **保持向后兼容**: 现有代码可渐进迁移
3. **合理设置 TTL**: 根据数据特性选择合适的 TTL
4. **使用二级索引**: 对于需要按非 ID 字段查询的数据
5. **Cache-Aside 模式**: 使用 `getOrLoad` 减少代码重复

---

## 版本历史

| 版本 | 日期 | 变更内容 |
|------|------|----------|
| 2.2.0 | 2025-02-17 | 完成 Auth Service 完整集成，支持加密回调和动态 TTL |
| 2.1.0 | 2025-02-17 | 添加 Game Data Service Redis Repository，支持货币原子操作和库存类型索引 |
| 2.0.0 | 2025-02-17 | 引入统一 Redis Repository 架构 |
| 1.0.0 | 2025-09-19 | 初始版本，直接使用 RedisPool |
