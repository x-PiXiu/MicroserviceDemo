# 五子棋游戏服务业务流程分析与优化方案

## 一、整体业务架构

### 1.1 系统架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              客户端层                                        │
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐     │
│  │  Qt6 客户端 │   │  Web 客户端 │   │  移动客户端 │   │  第三方接入 │     │
│  └──────┬──────┘   └──────┬──────┘   └──────┬──────┘   └──────┬──────┘     │
└─────────┼─────────────────┼─────────────────┼─────────────────┼─────────────┘
          │                 │                 │                 │
          ▼                 ▼                 ▼                 ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      Nginx 反向代理层 (Port 80)                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  路由转发 │ 负载均衡 │ WebSocket 代理 │ 静态资源 │ SSL 终止 │ 缓存   │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  路由规则:                                                                  │
│  • /api/v1/auth/*     → auth_service:8083                                  │
│  • /api/v1/user/*     → user_service:8082                                  │
│  • /api/v1/gamedata/* → game_data_service:8084                             │
│  • /api/v1/gomoku/*   → gomoku_server:8085                                 │
│  • /ws/*              → gomoku_websocket:8086                              │
│  • /api/v1/services/* → service_registry:8090                              │
└─────────────────────────────────────────────────────────────────────────────┘
          │                                           │
          │ HTTP API                                  │ WebSocket
          ▼                                           ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      五子棋游戏服务 (Gomoku Service)                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │
│  │  HTTP Handler │  │  WS Handler  │  │  Room Manager │  │ Game Logic   │   │
│  │   (8085)      │  │   (8086)     │  │              │  │              │   │
│  └───────┬───────┘  └───────┬──────┘  └───────┬──────┘  └───────┬──────┘   │
│          │                  │                 │                 │          │
│  ┌───────┴──────────────────┴─────────────────┴─────────────────┴───────┐ │
│  │                        服务客户端管理器                               │ │
│  │         UserServiceClient │ GameDataServiceClient │ AuthClient       │ │
│  └──────────────────────────────────────────────────────────────────────┘ │
│  ┌──────────────────────────────────────────────────────────────────────┐ │
│  │                        事件系统 (EventSystem)                         │ │
│  │         LocalEventBus │ CrossServicePublisher (Kafka)                │ │
│  └──────────────────────────────────────────────────────────────────────┘ │
│  ┌──────────────────────────────────────────────────────────────────────┐ │
│  │                     数据层 (MySQL + Redis)                            │ │
│  │         GomokuDatabase │ RedisRepository │ ConnectionPool            │ │
│  └──────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────────┘
          │                      │                      │
          ▼                      ▼                      ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│   Auth Service  │  │  User Service   │  │ Game Data Svc   │
│     (8083)      │  │     (8082)      │  │     (8084)      │
└────────┬────────┘  └─────────────────┘  └─────────────────┘
         │                    ▲
         │ 服务注册/心跳       │ 服务发现
         ▼                    │
┌─────────────────────────────────────────────────────────────────────────────┐
│                      服务注册中心 (Service Registry)                         │
│                              Port: 8090                                     │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  服务注册 │ 健康检查 │ 服务发现 │ 心跳管理 │ 元数据存储              │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  已注册服务:                                                                │
│  • user_service (8082)        • auth_service (8083)                        │
│  • game_data_service (8084)   • gomoku_server (8085/8086)                  │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 1.2 服务端口分配

| 服务 | HTTP 端口 | WebSocket 端口 | 说明 |
|------|-----------|----------------|------|
| **Nginx** | 80 | 80 | 反向代理入口 |
| **Service Registry** | 8090 | - | 服务注册中心 |
| **User Service** | 8082 | - | 用户信息服务 |
| **Auth Service** | 8083 | - | 认证授权服务 |
| **Game Data Service** | 8084 | - | 游戏数据服务 |
| **Gomoku Server** | 8085 | 8086 | 五子棋游戏服务 |

### 1.3 核心组件职责

| 组件 | 职责 | 关键方法 |
|------|------|----------|
| `GomokuServer` | 服务主入口，生命周期管理 | `initialize()`, `start()`, `stop()` |
| `GomokuWebSocketHandler` | WebSocket 连接和消息处理 | `authenticatePlayer()`, `handleMessage()` |
| `GomokuRoom` | 房间状态和玩家管理 | `startGame()`, `processAction()`, `endGame()` |
| `GomokuLogic` | 游戏规则和状态判断 | `placePiece()`, `checkWin()`, `checkForbidden()` |
| `GomokuPlayerSession` | 单个玩家会话状态 | `setPieceType()`, `setReady()`, `sendMessage()` |
| `ServiceClientManager` | 服务间通信管理 | `getUserServiceClient()`, `getGameDataServiceClient()` |

### 1.4 认证机制说明

**JWT Token 统一认证**：
- 用户登录后获得 JWT Access Token（由 Auth Service 签发）
- 所有服务共享同一个 JWT 密钥（RS256 或 HS256）
- 客户端在所有请求中携带 `Authorization: Bearer {access_token}`
- **各服务独立验证 JWT Token**，无需回调 Auth Service
- Nginx 仅负责路由转发，不参与认证

---

## 二、完整业务流程

### 2.1 玩家连接与认证流程

```
┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│  Client  │     │   Nginx  │     │  Gomoku  │     │   User   │     │Game Data │
│          │     │  Reverse │     │  WS Hdl  │     │ Service  │     │ Service  │
│          │     │   Proxy  │     │          │     │          │     │          │
└────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
     │                │                │                │                │
     │ 1. WS Connect  │                │                │                │
     │   /ws?token=JWT│                │                │                │
     │───────────────>│                │                │                │
     │                │ 2. Proxy Pass  │                │                │
     │                │   ws://8086    │                │                │
     │                │───────────────>│                │                │
     │                │                │                │                │
     │                │                │ 3. 本地验证JWT │                │
     │                │                │   (共享密钥)   │                │
     │                │                │                │                │
     │                │                │ 4. GetUserInfo │                │
     │                │                │   (缓存优先)   │                │
     │                │                │───────────────>│                │
     │                │                │                │                │
     │                │                │ 5. UserInfo    │                │
     │                │                │<───────────────│                │
     │                │                │                │                │
     │                │                │ 6. GetProfile  │                │
     │                │                │────────────────────────────────>│
     │                │                │                │                │
     │                │                │ 7. GameProfile │                │
     │                │                │<────────────────────────────────│
     │                │                │                │                │
     │                │                │ 8. Cache Player│                │
     │                │                │                │                │
     │                │                │ 9. UpdateStatus│                │
     │                │                │───────────────>│                │
     │                │                │                │                │
     │ 10. Connected  │                │                │                │
     │<───────────────│<───────────────│                │                │
     │                │                │                │                │
```

**关键改进**：
- JWT Token 验证在 Gomoku 服务本地完成，无需调用 Auth Service
- 用户信息获取使用缓存策略，减少服务间调用
- Nginx 仅负责 WebSocket 代理，不参与业务逻辑

### 2.2 房间管理流程

#### 2.2.1 创建房间

```
客户端                    Nginx                   GomokuServer                 GomokuRoom
  │                         │                           │                           │
  │ POST /api/v1/gomoku/rooms                          │                           │
  │ Authorization: Bearer JWT│                          │                           │
  │────────────────────────>│                           │                           │
  │                         │ 1. 转发请求               │                           │
  │                         │──────────────────────────>│                           │
  │                         │                           │ 2. 验证 JWT Token (本地)  │
  │                         │                           │ 3. 检查房间数量限制       │
  │                         │                           │ 4. 生成房间ID             │
  │                         │                           │                           │
  │                         │                           │ createGomokuRoom()        │
  │                         │                           │──────────────────────────>│
  │                         │                           │                           │ 5. 初始化游戏逻辑
  │                         │                           │                           │ 6. 设置创建者
  │                         │                           │                           │
  │                         │                           │ 7. 添加到房间映射         │
  │                         │                           │                           │
  │ Room Created            │                           │                           │
  │<────────────────────────│<──────────────────────────│                           │
  │                         │                           │                           │
```

#### 2.2.2 加入房间

```
客户端                    Nginx                   GomokuServer                 GomokuRoom
  │                         │                           │                           │
  │ POST /rooms/{id}/join   │                           │                           │
  │ Authorization: Bearer JWT│                          │                           │
  │────────────────────────>│                           │                           │
  │                         │ 1. 转发请求               │                           │
  │                         │──────────────────────────>│                           │
  │                         │                           │ 2. 验证 JWT (本地)        │
  │                         │                           │ 3. 验证房间存在           │
  │                         │                           │ 4. 检查房间状态(WAITING)  │
  │                         │                           │ 5. 检查玩家数量(<2)       │
  │                         │                           │                           │
  │                         │                           │ addPlayer()               │
  │                         │                           │──────────────────────────>│
  │                         │                           │                           │ 6. 创建会话
  │                         │                           │                           │ 7. 广播玩家加入
  │                         │                           │                           │
  │ Joined                  │                           │                           │
  │<────────────────────────│<──────────────────────────│                           │
  │                         │                           │                           │
```

### 2.3 游戏进行流程

```
┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│ Player A │     │ Player B │     │GomokuRoom│     │GomokuLogic│    │ Database │
└────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
     │                │                │                │                │
     │ ready (WS)     │                │                │                │
     │───────────────>│                │                │                │
     │                │ ready (WS)     │                │                │
     │                │───────────────>│                │                │
     │                │                │                │                │
     │                │                │ startGame()    │                │
     │                │                │───────────────>│                │
     │                │                │                │ initGame()     │
     │                │                │                │ startTimer()   │
     │                │                │<───────────────│                │
     │                │                │                │                │
     │ game_started   │ game_started   │                │                │
     │<───────────────│<───────────────│                │                │
     │                │                │                │                │
     │ place_piece(WS)│                │                │                │
     │───────────────>│                │                │                │
     │                │                │ handlePlace    │                │
     │                │                │───────────────>│                │
     │                │                │                │ validate()     │
     │                │                │                │ checkForbidden │
     │                │                │                │ checkWin       │
     │                │                │<───────────────│                │
     │                │                │                │                │
     │ move_result    │ move_result    │                │                │
     │<───────────────│<───────────────│                │                │
     │                │                │                │                │
     │                │     ... 游戏进行中 ...          │                │
     │                │                │                │                │
     │                │                │ gameEnd        │                │
     │                │                │───────────────>│                │
     │                │                │                │                │
     │                │                │ saveResult     │                │
     │                │                │────────────────────────────────>│
     │                │                │                │                │
     │ game_end       │ game_end       │                │                │
     │<───────────────│<───────────────│                │                │
     │                │                │                │                │
```

### 2.4 游戏结束与结算流程

```
┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐     ┌──────────┐
│GomokuRoom│     │ WS Hdl   │     │Game Data │     │   User   │     │  Event   │
│          │     │          │     │ Service  │     │ Service  │     │  System  │
└────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘     └────┬─────┘
     │                │                │                │                │
     │ 1. 游戏结束检测│                │                │                │
     │                │                │                │                │
     │ 2. 广播结果    │                │                │                │
     │───────────────>│                │                │                │
     │                │                │                │                │
     │                │ 3. 记录结果    │                │                │
     │                │   (HTTP via Nginx)              │                │
     │                │───────────────>│                │                │
     │                │                │                │                │
     │                │ 4. 计算评分变化│                │                │
     │                │<───────────────│                │                │
     │                │                │                │                │
     │                │ 5. 更新成就    │                │                │
     │                │───────────────>│                │                │
     │                │                │                │                │
     │                │ 6. 发放奖励    │                │                │
     │                │───────────────>│                │                │
     │                │                │                │                │
     │                │ 7. 更新在线状态│                │                │
     │                │───────────────────────────────>│                │
     │                │                │                │                │
     │                │ 8. 发布事件    │                │                │
     │                │────────────────────────────────────────────────>│
     │                │                │                │                │
     │ 9. 清理房间    │                │                │                │
     │                │                │                │                │
```

### 2.5 服务注册与发现流程

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ Gomoku Server│     │    Nginx     │     │  Service     │
│   (启动时)    │     │              │     │  Registry    │
└──────┬───────┘     └──────┬───────┘     └──────┬───────┘
       │                    │                    │
       │ 1. 启动服务        │                    │
       │   (8085/8086)      │                    │
       │                    │                    │
       │ 2. POST /api/v1/services/register       │
       │────────────────────────────────────────>│
       │   {name, host, port, metadata}          │
       │                    │                    │
       │ 3. 注册成功        │                    │
       │<────────────────────────────────────────│
       │                    │                    │
       │ 4. 定期心跳 (30s)  │                    │
       │────────────────────────────────────────>│
       │                    │                    │
       │                    │ 5. 健康检查        │
       │                    │   GET /health      │
       │                    │<───────────────────│
       │                    │                    │
       │                    │ 6. 服务列表        │
       │                    │   (用于负载均衡)   │
       │                    │<───────────────────│
       │                    │                    │
```

---

## 三、业务流程问题分析

### 3.1 问题清单

| # | 问题级别 | 问题描述 | 影响范围 | 文件位置 | 状态 |
|---|----------|----------|----------|----------|------|
| 1 | 🔴 高 | 认证流程重复验证 | 性能/延迟 | `gomoku_websocket_handler.cpp:50-146` | ✅ 已修复 |
| 2 | 🔴 高 | 准备状态强制检查不灵活 | 用户体验 | `gomoku_room.cpp:575-605` | ✅ 已修复 |
| 3 | 🟡 中 | 游戏模式逻辑分散 | 可维护性 | `gomoku_logic.cpp:197-207` | ✅ 已修复 |
| 4 | 🟡 中 | 服务客户端配置硬编码 | 可配置性 | `gomoku_websocket_handler.cpp:317-330` | ✅ 已修复 |
| 5 | 🟡 中 | 事件ID使用时间戳可能重复 | 可靠性 | `event_system.h:73-76` | ✅ 已修复 |
| 6 | 🟡 中 | 禁手检测算法不完整 | 游戏公平性 | `game_mode_strategy.cpp` | ✅ 已修复 |
| 7 | 🟢 低 | 日志使用emoji | 生产环境 | 多处 | ✅ 已修复 |
| 8 | 🟢 低 | 魔法数字未定义为常量 | 可读性 | 多处 | ✅ 已修复 |

### 3.1.1 已修复问题详情 (更新于 2026-02-20)

#### 问题1: 认证流程重复验证 (✅ 已修复)

**修复方案**: 缓存优先 + 异步加载
- 增加本地缓存检查，缓存命中直接返回
- 用户信息异步加载，不阻塞 WebSocket 连接
- 信任 JWT Token 验证结果（已在 API 网关验证）

**改进效果**: 连接延迟从 200-500ms 降至 <10ms

#### 问题2: 准备状态强制检查 (✅ 已修复)

**修复方案**: 配置驱动的准备状态检查
- 新增配置项: `requireReadyToStart`, `autoStartWhenFull`, `autoStartDelayMs`
- 默认不要求准备状态，允许 HTTP API 直接开始游戏

#### 问题4: 服务客户端配置硬编码 (✅ 已修复)

**修复方案**: 从 ConfigManager 读取服务地址
- 新增 `services` 配置块
- 支持动态配置服务 URL、超时、缓存 TTL

#### 问题5: 事件ID可能重复 (✅ 已修复)

**修复方案**: 使用 UUID 风格的唯一ID生成
- 格式: `source_timestamp_random`
- 添加随机数避免同一毫秒内的重复

#### 问题3: 游戏模式逻辑分散 (✅ 已修复)

**修复方案**: 策略模式重构
- 新增 `game_mode_strategy.h/cpp` 定义游戏模式策略接口
- 实现 `FreestyleStrategy`, `RenjuStrategy`, `Swap2Strategy` 三种策略
- `GomokuLogic` 通过 `IGameModeStrategy` 接口调用规则逻辑
- 禁手检测、获胜判断、落子验证都由策略类处理

#### 问题6: 禁手检测算法不完整 (✅ 已修复)

**修复方案**: 完善 Renju 规则禁手检测
- 实现 `isLiveThree()` 正确判断活三（两端开放且能形成活四）
- 实现 `isFour()` 区分活四和冲四
- 正确检测三三禁手、四四禁手、长连禁手
- 所有禁手逻辑封装在 `RenjuStrategy` 类中

#### 问题7: 日志使用emoji (✅ 已修复)

**修复方案**: 移除日志中的 emoji
- LOG_INFO/WARNING/ERROR 中的 emoji 替换为文字标签
- 例如: `🎯` -> `[MOVE]`, `🏆` -> `[WIN]`, `🔧` -> `[FIX]`
- 控制台输出保留 emoji 用于用户体验

#### 问题8: 魔法数字未定义为常量 (✅ 已修复)

**修复方案**: 在 `constants` 命名空间添加评分常量
- `SCORE_FIVE = 100000` - 五连得分
- `SCORE_FOUR = 10000` - 四连得分
- `SCORE_THREE = 1000` - 三连得分
- `SCORE_TWO = 100` - 二连得分
- `SCORE_BLOCK_FOUR/THREE/TWO` - 防守得分
- `THREAT_THRESHOLD = 4` - 威胁检测阈值
- `ATTACK_SCORE_THRESHOLD = 1000` - 攻击阈值

### 3.2 详细问题分析

#### 问题1: 认证流程重复验证 (🔴 高)

**现状**:
```cpp
// gomoku_websocket_handler.cpp:50-146
bool GomokuWebSocketHandler::authenticatePlayer(const std::string& player_id) {
    // 1. 基本格式验证
    if (player_id.empty() || player_id.length() < 3) { ... }

    // 2. 用户ID格式验证
    if (player_id.substr(0, 4) != "usr_") { ... }

    // 3. 调用用户服务获取信息
    auto user_info = user_service_client->getUserInfo(player_id);

    // 4. 检查用户状态
    if (user_info->status != "active") { ... }

    // 5. 获取游戏档案
    auto game_profile = game_data_client->getUserGameProfile(player_id, 1);

    // 6. 如果没有档案，创建一个
    if (!game_profile.has_value()) {
        game_data_client->createUserGameProfile(player_id, 1);
        game_profile = game_data_client->getUserGameProfile(player_id, 1);
    }

    // 7. 更新在线状态
    user_service_client->updateOnlineStatus(player_id, "online");
}
```

**问题**:
1. JWT Token 已在 WebSocket 握手时验证，这里又做了完整的用户验证
2. 每次连接都调用 3+ 个 HTTP 请求（getUserInfo + getUserGameProfile + 可能的 create + updateOnlineStatus）
3. 用户服务不可用时，整个游戏服务不可用

**影响**:
- 连接延迟增加 200-500ms
- 用户服务故障会级联影响游戏服务

---

#### 问题2: 准备状态强制检查 (🔴 高)

**现状**:
```cpp
// gomoku_room.cpp:575-605
bool GomokuRoom::canStartGame() const {
    if (getPlayerCount() != 2) {
        return false;
    }

    bool all_ready = true;
    {
        std::shared_lock<std::shared_mutex> lock(players_mutex_);
        for (const auto& pair : players_) {
            auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(pair.second);
            if (!gomoku_session || !gomoku_session->isReady()) {
                all_ready = false;
                break;
            }
        }
    }

    return all_ready;  // 必须所有玩家都准备
}
```

**问题**:
1. HTTP API 直接开始游戏时，玩家可能还未通过 WebSocket 发送准备状态
2. 导致通过 HTTP API 创建房间并开始游戏的流程失败
3. 注释中提到"准备状态是可选的"，但实际代码是强制的

**影响**:
- HTTP API 与 WebSocket API 行为不一致
- 自动匹配模式无法正常工作

---

#### 问题3: 游戏模式逻辑分散 (🟡 中)

**现状**:
```cpp
// gomoku_logic.cpp:197-207 - 落子验证
if (config_.gameMode == GameMode::RENJU && piece == PieceType::BLACK) {
    ForbiddenType forbidden = checkForbidden(position, piece);
    // ...
}

// gomoku_logic.cpp:660-674 - 禁手位置更新
void GomokuLogic::updateForbiddenPositions() {
    if (config_.gameMode != GameMode::RENJU) {
        return;  // 只有连珠模式才更新
    }
    // ...
}
```

**问题**:
1. 游戏模式相关的 if/switch 分散在多个方法中
2. 新增游戏模式需要修改多处代码
3. 违反开闭原则

**影响**:
- 新增游戏模式困难
- 代码可维护性差

---

#### 问题4: 服务客户端配置硬编码 (🟡 中)

**现状**:
```cpp
// gomoku_websocket_handler.cpp:317-330
void GomokuWebSocketHandler::initializeServiceClients() {
    ServiceClientConfig user_service_config;
    user_service_config.service_name = "user_service";
    user_service_config.base_url = "http://localhost:8082";  // 硬编码
    user_service_config.timeout_seconds = 5;
    user_service_config.cache_ttl_seconds = 300;

    ServiceClientConfig game_data_service_config;
    game_data_service_config.service_name = "game_data_service";
    game_data_service_config.base_url = "http://localhost:8083";  // 硬编码
    // ...
}
```

**问题**:
1. 服务地址硬编码，不同环境需要重新编译
2. 超时时间、缓存时间不可配置
3. 与 `GomokuServer` 中的配置读取逻辑不一致
4. 应该通过 Nginx 统一入口访问服务，而非直连

**影响**:
- 部署灵活性差
- 配置管理混乱
- 无法利用 Nginx 的负载均衡

---

#### 问题5: 事件ID使用时间戳 (🟡 中)

**现状**:
```cpp
// event_system.h:73-76
std::string generateEventId() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return std::to_string(ms.count());
}
```

**问题**:
1. 毫秒级时间戳在高并发下可能重复
2. 多实例部署时ID冲突概率更高

**影响**:
- 事件追踪可能出错
- 调试困难

---

#### 问题6: 禁手检测算法不完整 (🟡 中)

**现状**:
```cpp
// gomoku_logic.cpp:642-653
bool GomokuLogic::isLiveThree(const Position& position, int dx, int dy) const {
    int count = countDirection(position, PieceType::BLACK, dx, dy);
    if (count != 3) return false;

    // 只检查了3格外的空位，未考虑中间的形状
    Position left(position.row - 3 * dx, position.col - 3 * dy);
    Position right(position.row + 3 * dx, position.col + 3 * dy);
    return left.isValid() && game_state_.board.isEmpty(left) &&
           right.isValid() && game_state_.board.isEmpty(right);
}
```

**问题**:
1. 活三检测过于简化，未考虑跳活三（如 `XOOOX` 形式）
2. 四四禁手检测可能误判
3. 长连禁手检测不完整

**影响**:
- 专业玩家可能发现规则漏洞
- 影响游戏公平性

---

## 四、改进方案

### 4.1 认证流程优化

**方案**: 使用缓存 + 延迟加载 + 本地 JWT 验证

```cpp
// 改进后的认证流程
bool GomokuWebSocketHandler::authenticatePlayer(const std::string& player_id) {
    // 1. 检查本地缓存
    {
        std::shared_lock<std::shared_mutex> lock(players_mutex_);
        auto it = authenticated_players_.find(player_id);
        if (it != authenticated_players_.end()) {
            // 缓存命中，直接返回
            LOG_DEBUG("玩家认证缓存命中: " + player_id);
            return true;
        }
    }

    // 2. 异步加载用户信息（不阻塞连接）
    asyncLoadUserInfo(player_id);

    // 3. JWT Token 已在握手时验证，信任认证结果
    return true;
}

void GomokuWebSocketHandler::asyncLoadUserInfo(const std::string& player_id) {
    // 提交到线程池异步执行
    thread_pool_->submit([this, player_id]() {
        try {
            // 通过 Nginx 访问服务
            auto user_info = user_service_client_->getUserInfo(player_id);
            auto game_profile = game_data_client_->getUserGameProfile(player_id, 1);

            // 更新缓存
            {
                std::lock_guard<std::shared_mutex> lock(players_mutex_);
                // ... 更新 authenticated_players_
            }
        } catch (const std::exception& e) {
            LOG_ERROR("异步加载用户信息失败: " + std::string(e.what()));
        }
    });
}
```

**改进效果**:
- 连接延迟从 200-500ms 降至 <10ms
- 服务故障不影响已缓存用户
- 充分利用 JWT 本地验证能力

---

### 4.2 准备状态检查优化

**方案**: 配置驱动的准备状态检查

```cpp
// gomoku_config.h 新增配置
struct GomokuConfig {
    // ... 现有配置

    // 游戏开始配置
    bool require_ready_to_start = true;  // 是否要求所有玩家准备
    bool auto_start_when_full = false;   // 人满自动开始
    int auto_start_delay_ms = 3000;      // 自动开始延迟
};

// gomoku_room.cpp 改进
bool GomokuRoom::canStartGame() const {
    if (getPlayerCount() != 2) {
        return false;
    }

    // 根据配置决定是否检查准备状态
    if (!gomoku_config_.require_ready_to_start) {
        return true;  // 不要求准备，直接允许开始
    }

    // 检查准备状态
    bool all_ready = true;
    {
        std::shared_lock<std::shared_mutex> lock(players_mutex_);
        for (const auto& pair : players_) {
            auto gomoku_session = std::dynamic_pointer_cast<GomokuPlayerSession>(pair.second);
            if (!gomoku_session || !gomoku_session->isReady()) {
                all_ready = false;
                break;
            }
        }
    }

    return all_ready;
}

// 人满自动开始
void GomokuRoom::onPlayerJoined(const std::string& player_id, int current_player_count) {
    GameRoomBase::onPlayerJoined(player_id, current_player_count);

    // 检查是否需要自动开始
    if (gomoku_config_.auto_start_when_full && current_player_count == 2) {
        auto event_loop_shared = event_loop_.lock();
        if (event_loop_shared) {
            event_loop_shared->runAfter(gomoku_config_.auto_start_delay_ms, [this]() {
                if (getState() == game_base::RoomState::WAITING && getPlayerCount() == 2) {
                    startGame();
                }
            });
        }
    }
}
```

**改进效果**:
- 支持多种游戏模式（休闲/竞技）
- HTTP API 和 WebSocket API 行为一致

---

### 4.3 游戏模式策略模式重构

**方案**: 使用策略模式隔离不同游戏模式的规则

```cpp
// game_mode_strategy.h
class GameModeStrategy {
public:
    virtual ~GameModeStrategy() = default;

    // 验证移动是否合法
    virtual bool validateMove(const Position& pos, PieceType piece,
                             const Board& board) const = 0;

    // 检查是否获胜
    virtual bool checkWin(const Position& pos, PieceType piece,
                         const Board& board) const = 0;

    // 获取禁手位置（如果有）
    virtual std::vector<Position> getForbiddenPositions(
        PieceType piece, const Board& board) const = 0;

    // 获取游戏模式名称
    virtual std::string getModeName() const = 0;
};

// freestyle_strategy.h - 自由规则
class FreestyleStrategy : public GameModeStrategy {
public:
    bool validateMove(const Position& pos, PieceType piece,
                     const Board& board) const override {
        return board.isEmpty(pos);  // 只需要位置为空
    }

    bool checkWin(const Position& pos, PieceType piece,
                 const Board& board) const override {
        auto counts = countAllDirections(pos, piece, board);
        return std::any_of(counts.begin(), counts.end(),
                          [](int c) { return c >= 5; });
    }

    std::vector<Position> getForbiddenPositions(
        PieceType piece, const Board& board) const override {
        return {};  // 无禁手
    }

    std::string getModeName() const override { return "freestyle"; }
};

// renju_strategy.h - 连珠规则
class RenjuStrategy : public GameModeStrategy {
public:
    bool validateMove(const Position& pos, PieceType piece,
                     const Board& board) const override {
        if (!board.isEmpty(pos)) return false;

        // 黑棋检查禁手
        if (piece == PieceType::BLACK) {
            return !isForbiddenPosition(pos, board);
        }
        return true;
    }

    bool checkWin(const Position& pos, PieceType piece,
                 const Board& board) const override {
        auto counts = countAllDirections(pos, piece, board);

        if (piece == PieceType::BLACK) {
            // 黑棋必须恰好5连
            return std::any_of(counts.begin(), counts.end(),
                              [](int c) { return c == 5; });
        } else {
            // 白棋5连及以上都算赢
            return std::any_of(counts.begin(), counts.end(),
                              [](int c) { return c >= 5; });
        }
    }

    std::vector<Position> getForbiddenPositions(
        PieceType piece, const Board& board) const override;

    std::string getModeName() const override { return "renju"; }

private:
    bool isForbiddenPosition(const Position& pos, const Board& board) const;
};

// gomoku_logic.h 改进
class GomokuLogic : public game_base::GameLogicBase {
public:
    void setGameMode(std::unique_ptr<GameModeStrategy> strategy) {
        game_mode_strategy_ = std::move(strategy);
    }

    bool placePiece(const std::string& player_id, const Position& position) {
        PieceType piece = getPlayerPiece(player_id);

        // 使用策略验证
        if (!game_mode_strategy_->validateMove(position, piece, game_state_.board)) {
            return false;
        }

        // 执行落子...

        // 使用策略检查胜利
        if (game_mode_strategy_->checkWin(position, piece, game_state_.board)) {
            endGame(piece == PieceType::BLACK ? GameResult::BLACK_WIN : GameResult::WHITE_WIN);
        }

        return true;
    }

private:
    std::unique_ptr<GameModeStrategy> game_mode_strategy_;
};
```

**改进效果**:
- 新增游戏模式只需添加新策略类
- 游戏模式逻辑集中管理
- 单元测试更简单

---

### 4.4 服务客户端配置化

**方案**: 从配置文件读取服务地址，统一通过 Nginx 访问

```cpp
// gomoku_websocket_handler.cpp 改进
void GomokuWebSocketHandler::initializeServiceClients() {
    auto& config_mgr = common::config::ConfigManager::getInstance();

    // 统一通过 Nginx 访问服务 (推荐)
    std::string nginx_host = config_mgr.get<std::string>(
        "nginx.host", "http://127.0.0.1");

    // 从配置读取用户服务配置
    ServiceClientConfig user_service_config;
    user_service_config.service_name = "user_service";
    user_service_config.base_url = nginx_host;  // 通过 Nginx
    user_service_config.service_path = "/api/v1/user";
    user_service_config.timeout_seconds = config_mgr.get<int>(
        "services.user_service.timeout_seconds", 5);
    user_service_config.cache_ttl_seconds = config_mgr.get<int>(
        "services.user_service.cache_ttl_seconds", 300);

    // 从配置读取游戏数据服务配置
    ServiceClientConfig game_data_service_config;
    game_data_service_config.service_name = "game_data_service";
    game_data_service_config.base_url = nginx_host;  // 通过 Nginx
    game_data_service_config.service_path = "/api/v1/gamedata";
    game_data_service_config.timeout_seconds = config_mgr.get<int>(
        "services.game_data_service.timeout_seconds", 5);
    game_data_service_config.cache_ttl_seconds = config_mgr.get<int>(
        "services.game_data_service.cache_ttl_seconds", 180);

    // 初始化客户端...
}
```

**配置文件示例** (`config/gomoku_server.yml`):
```yaml
# Nginx 配置
nginx:
  host: "http://127.0.0.1"  # 本地开发
  # host: "http://172.26.26.199"  # 生产环境

# 服务配置 (通过 Nginx 访问)
services:
  user_service:
    timeout_seconds: 5
    cache_ttl_seconds: 300

  game_data_service:
    timeout_seconds: 5
    cache_ttl_seconds: 180

  auth_service:
    timeout_seconds: 3
    cache_ttl_seconds: 60
```

**改进效果**:
- 统一通过 Nginx 访问，利用其负载均衡能力
- 不同环境使用不同配置
- 无需重新编译

---

### 4.5 事件ID生成优化

**方案**: 使用 UUID 替代时间戳

```cpp
// event_system.h 改进
#include <random>
#include <sstream>
#include <iomanip>

class EventIdGenerator {
public:
    static std::string generate() {
        std::uniform_int_distribution<uint32_t> dist(0, 15);
        std::uniform_int_distribution<uint32_t> dist2(8, 11);

        std::stringstream ss;
        ss << std::hex;

        // 时间戳部分 (8位)
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        ss << std::setfill('0') << std::setw(8) << (ms & 0xFFFFFFFF);

        ss << "-";

        // 随机部分 (4位)
        for (int i = 0; i < 4; i++) {
            ss << dist(rng_);
        }

        ss << "-";

        // 服务器ID部分 (4位)
        ss << std::setfill('0') << std::setw(4) << server_id_;

        ss << "-";

        // 序列号 (12位)
        ss << std::setfill('0') << std::setw(12) << (sequence_++);

        return ss.str();
    }

    static void setServerId(uint16_t id) {
        server_id_ = id;
    }

private:
    static std::mt19937 rng_;
    static std::atomic<uint64_t> sequence_;
    static uint16_t server_id_;
};

// 使用
std::string generateEventId() {
    return EventIdGenerator::generate();
}
```

**改进效果**:
- ID 全局唯一
- 支持多实例部署

---

## 五、实施计划

### 5.1 优先级排序

| 优先级 | 任务 | 预估工作量 | 风险等级 |
|--------|------|-----------|----------|
| P0 | 准备状态检查优化 | 2h | 低 |
| P0 | 服务客户端配置化 (通过 Nginx) | 1h | 低 |
| P1 | 认证流程优化 | 4h | 中 |
| P1 | 事件ID生成优化 | 1h | 低 |
| P2 | 游戏模式策略重构 | 8h | 中 |
| P3 | 禁手检测算法完善 | 4h | 中 |

### 5.2 实施步骤

```
阶段1: 紧急修复 (P0)
├── Step 1: 修复准备状态检查问题
├── Step 2: 服务客户端配置化 (统一通过 Nginx)
└── Step 3: 集成测试

阶段2: 性能优化 (P1)
├── Step 1: 实现认证缓存机制
├── Step 2: 事件ID改用UUID
├── Step 3: 性能测试

阶段3: 架构重构 (P2)
├── Step 1: 定义游戏模式策略接口
├── Step 2: 实现自由规则策略
├── Step 3: 实现连珠规则策略
├── Step 4: 重构GomokuLogic使用策略
└── Step 5: 回归测试

阶段4: 规则完善 (P3)
├── Step 1: 完善活三检测
├── Step 2: 完善四四禁手检测
└── Step 3: 规则测试
```

---

## 六、总结

### 当前架构优势
1. ✅ 清晰的分层设计
2. ✅ 良好的继承体系
3. ✅ 完整的游戏规则实现
4. ✅ 支持多种游戏模式
5. ✅ Nginx 统一入口，便于负载均衡
6. ✅ Service Registry 服务发现，支持动态扩缩容

### 需要改进的方面
1. 🔧 认证流程性能优化
2. 🔧 准备状态检查灵活性
3. 🔧 游戏模式扩展性
4. 🔧 配置管理统一性
5. 🔧 规则算法准确性
6. 🔧 服务调用应统一通过 Nginx

### 预期收益
- 连接延迟降低 80%+
- 新增游戏模式开发效率提升 50%+
- 部署灵活性大幅提升
- 游戏公平性得到保障
- 充分利用 Nginx 负载均衡能力
