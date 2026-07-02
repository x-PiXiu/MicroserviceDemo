# 微服务架构完整重构方案

## 背景

当前项目存在以下问题需要解决：
1. **Redis 操作分散**：每个服务直接使用 RedisConnection，无统一抽象层
2. **键命名不规范**：`session:{id}`, `user:info:{id}`, `gomoku:test` 等不一致
3. **TTL 硬编码**：缓存过期时间分散在各服务中
4. **缺乏统一降级策略**：Redis 故障时处理不一致
5. **序列化方式不统一**：各服务使用不同的序列化方法

## 目标

建立统一的 Repository 模式，包括：
- MySQL Repository（已有基础架构）
- Redis Repository（新建抽象层）
- 所有服务统一使用新架构

---

## 第一阶段：Redis 抽象层（新组件）

### 1.1 RedisKeyBuilder - 统一键名管理

**文件**: `include/common/repository/redis_key_builder.h`

```cpp
// 键命名规范：{service}:{entity}:{id}
class RedisKeyBuilder {
public:
    // 服务命名空间
    static constexpr const char* AUTH_SERVICE = "auth";
    static constexpr const char* USER_SERVICE = "user";
    static constexpr const char* GAME_SERVICE = "game";
    static constexpr const char* GOMOKU_SERVICE = "gomoku";

    // 通用键构建
    static std::string entityKey(const std::string& service,
                                  const std::string& entity,
                                  const std::string& id);

    // 服务专用键构建器
    struct UserService {
        static std::string user(const std::string& user_id);
        static std::string profile(const std::string& user_id);
        static std::string usernameIndex(const std::string& username);
    };

    struct AuthService {
        static std::string session(const std::string& session_id);
        static std::string userSessions(const std::string& user_id);
    };

    struct GomokuService {
        static std::string userStats(const std::string& user_id);
        static std::string gameRecord(int64_t game_id);
        static std::string leaderboard(const std::string& game_mode);
    };
};
```

### 1.2 RedisTTLConfig - 集中 TTL 管理

**文件**: `include/common/repository/redis_ttl_config.h`

```cpp
class RedisTTLConfig {
public:
    static constexpr std::chrono::seconds SHORT_TTL{60};      // 1分钟
    static constexpr std::chrono::seconds DEFAULT_TTL{3600};  // 1小时
    static constexpr std::chrono::seconds LONG_TTL{86400};    // 24小时

    struct UserService {
        static constexpr std::chrono::seconds USER_INFO{3600};
        static constexpr std::chrono::seconds USERNAME_INDEX{86400};
    };

    struct AuthService {
        static constexpr std::chrono::seconds SESSION{3600};
    };

    struct GomokuService {
        static constexpr std::chrono::seconds LEADERBOARD{300};
        static constexpr std::chrono::seconds ROOM_STATE{7200};
    };
};
```

### 1.3 IRedisRepository 接口

**文件**: `include/common/repository/i_redis_repository.h`

```cpp
template<typename T, typename IdType = std::string>
class IRedisRepository {
public:
    virtual ~IRedisRepository() = default;

    // 基础 CRUD
    virtual RedisResult<std::optional<T>> findById(const IdType& id) = 0;
    virtual RedisResult<bool> save(const IdType& id, const T& entity,
                                    std::chrono::seconds ttl = 0) = 0;
    virtual RedisResult<bool> remove(const IdType& id) = 0;
    virtual RedisResult<bool> exists(const IdType& id) = 0;

    // 缓存操作
    virtual RedisResult<bool> invalidate(const IdType& id) = 0;
    virtual RedisResult<bool> refreshTTL(const IdType& id,
                                          std::chrono::seconds ttl) = 0;

    // 索引操作
    virtual RedisResult<bool> createIndex(const std::string& index_name,
                                           const std::string& index_value,
                                           const IdType& id) = 0;
};
```

### 1.4 BaseRedisRepository 实现

**文件**: `include/common/repository/base_redis_repository.h`

```cpp
template<typename T, typename IdType = std::string>
class BaseRedisRepository : public IRedisRepository<T, IdType> {
public:
    BaseRedisRepository(
        std::shared_ptr<database::RedisPool> redis_pool,
        const std::string& service_name,
        const std::string& entity_name,
        std::chrono::seconds default_ttl = RedisTTLConfig::DEFAULT_TTL
    );

    // 实现 IRedisRepository 所有方法

protected:
    // 子类需要实现
    virtual nlohmann::json toJson(const T& entity) const = 0;
    virtual std::optional<T> fromJson(const nlohmann::json& json) const = 0;
    virtual std::string buildKey(const IdType& id) const;
};
```

---

## 第二阶段：服务专用 Redis Repository

### 2.1 User Service

**新建文件**:
- `src/core_services/user_service/include/user_redis_repository.h`
- `src/core_services/user_service/src/user_redis_repository.cpp`

```cpp
class UserRedisRepository : public BaseRedisRepository<UserInfo> {
public:
    RedisResult<std::optional<UserInfo>> findByUsername(const std::string& username);
    RedisResult<bool> createUsernameIndex(const std::string& username,
                                           const std::string& user_id);
    RedisResult<bool> invalidateUserCache(const std::string& user_id,
                                           const std::string& username);
};

class UserProfileRedisRepository : public BaseRedisRepository<UserProfile> { };
class UserPreferencesRedisRepository : public BaseRedisRepository<UserPreferences> { };
```

### 2.2 Auth Service

**新建文件**:
- `src/core_services/auth_service/include/session_redis_repository.h`
- `src/core_services/auth_service/src/session_redis_repository.cpp`

```cpp
class SessionRedisRepository : public BaseRedisRepository<UserSession> {
public:
    RedisResult<std::vector<UserSession>> getUserSessions(const std::string& user_id);
    RedisResult<bool> addToUserSessions(const std::string& user_id,
                                         const std::string& session_id);
    RedisResult<int> deleteAllUserSessions(const std::string& user_id);
};

class DeviceRedisRepository : public BaseRedisRepository<DeviceInfo> { };
```

### 2.3 Gomoku Service

**新建文件**:
- `src/game_services/gomoku/include/gomoku_redis_repository.h`
- `src/game_services/gomoku/src/gomoku_redis_repository.cpp`

```cpp
class UserStatsRedisRepository : public BaseRedisRepository<UserGameStats> { };
class GameRecordRedisRepository : public BaseRedisRepository<GameRecord, int64_t> { };

class LeaderboardRedisRepository {
public:
    RedisResult<bool> updateScore(const std::string& game_mode,
                                   const std::string& user_id, double score);
    RedisResult<std::vector<LeaderboardEntry>> getRange(
        const std::string& game_mode, int start, int stop);
};

class RoomStateRedisRepository {
public:
    RedisResult<bool> saveRoomState(const std::string& room_id,
                                     const nlohmann::json& state);
};
```

---

## 第三阶段：服务层重构

### 3.1 User Service 重构

**修改文件**:
- `src/core_services/user_service/include/user_repository.h`
- `src/core_services/user_service/src/user_repository.cpp`

**重构内容**:
1. 替换直接 `RedisPool` 调用为 `UserRedisRepository`
2. 使用 `RedisKeyBuilder` 生成键名
3. 使用 `RedisTTLConfig` 获取 TTL

```cpp
class UserRepository {
private:
    std::shared_ptr<MySQLPool> mysql_pool_;
    // 新增：类型化 Redis 仓储
    std::unique_ptr<UserRedisRepository> user_redis_;
    std::unique_ptr<UserProfileRedisRepository> profile_redis_;
};
```

### 3.2 Auth Service 重构

**修改文件**:
- `src/core_services/auth_service/include/session_manager.h`
- `src/core_services/auth_service/src/session_manager.cpp`

**重构内容**:
1. 使用 `SessionRedisRepository` 管理会话
2. 使用 `RedisKeyBuilder` 生成键名
3. 统一会话 TTL 管理

### 3.3 Gomoku Service 重构

**修改文件**:
- `src/game_services/gomoku/include/gomoku_database.h`
- `src/game_services/gomoku/src/gomoku_database.cpp`

**重构内容**:
1. 添加 Redis 缓存层
2. 使用新的 Gomoku Redis Repository
3. 实现 Cache-Aside 模式

---

## 文件结构

### 新建文件（8个头文件 + 8个实现文件）

```
include/common/repository/
├── redis_key_builder.h        # 键名构建器
├── redis_ttl_config.h         # TTL 配置
├── i_redis_repository.h       # Redis 仓储接口
└── base_redis_repository.h    # Redis 仓储基类

src/core_services/user_service/
├── include/user_redis_repository.h
└── src/user_redis_repository.cpp

src/core_services/auth_service/
├── include/session_redis_repository.h
└── src/session_redis_repository.cpp

src/game_services/gomoku/
├── include/gomoku_redis_repository.h
└── src/gomoku_redis_repository.cpp
```

### 修改文件（6个）

```
src/core_services/user_service/
├── include/user_repository.h
└── src/user_repository.cpp

src/core_services/auth_service/
├── include/session_manager.h
└── src/session_manager.cpp

src/game_services/gomoku/
├── include/gomoku_database.h
└── src/gomoku_database.cpp
```

---

## 实施顺序

1. **步骤1**: 创建 Redis 抽象层基础组件
   - `redis_key_builder.h/cpp`
   - `redis_ttl_config.h`
   - `i_redis_repository.h`
   - `base_redis_repository.h/cpp`

2. **步骤2**: 创建 User Service Redis Repository
   - 实现 `UserRedisRepository`
   - 实现 `UserProfileRedisRepository`
   - 实现 `UserPreferencesRedisRepository`

3. **步骤3**: 重构 User Service
   - 修改 `UserRepository` 使用新组件

4. **步骤4**: 创建 Auth Service Redis Repository
   - 实现 `SessionRedisRepository`
   - 实现 `DeviceRedisRepository`

5. **步骤5**: 重构 Auth Service
   - 修改 `SessionManager` 使用新组件

6. **步骤6**: 创建 Gomoku Service Redis Repository
   - 实现 `UserStatsRedisRepository`
   - 实现 `GameRecordRedisRepository`
   - 实现 `LeaderboardRedisRepository`
   - 实现 `RoomStateRedisRepository`

7. **步骤7**: 重构 Gomoku Service
   - 修改 `GomokuDatabase` 使用新组件

---

## 验证方法

1. **单元测试**: 每个 Redis Repository 的独立测试
2. **集成测试**: MySQL + Redis 集成测试
3. **缓存测试**: 验证缓存命中/失效逻辑
4. **性能测试**: 对比重构前后延迟

---

## 关键依赖文件

| 文件 | 用途 |
|------|------|
| `include/common/repository/base_repository.h` | MySQL Repository 模式参考 |
| `include/common/repository/cache_manager.h` | 现有缓存抽象 |
| `include/common/database/redis_pool.h` | Redis 连接池 |
| `src/core_services/user_service/src/user_repository.cpp` | 当前 Redis 使用示例 |
| `include/common/session/distributed_session_manager.h` | 分布式会话参考 |
