# 🎯 五子棋游戏服务数据库设计分析与服务间数据流文档

## 📅 **文档信息**
- **创建日期**: 2025年9月27日
- **版本**: v2.1.0 (修复链接错误后)
- **范围**: 数据库表结构分析 + 微服务数据流设计
- **目的**: 全面理解五子棋服务的数据架构和服务间关系

---

## 🔍 **问题修复总结**

### **🚨 链接错误修复**
**问题**: `HttpServer::initialize()` 方法未定义
```bash
undefined reference to `common::http::HttpServer::initialize()'
```

**根因**: HttpServer类在头文件中声明了`initialize()`方法，但实现文件中只有`initializeWithoutChannelRegistration()`

**修复方案**: 在`http_server.cpp`中添加`initialize()`方法实现
```cpp
bool HttpServer::initialize() {
    LOG_INFO("Initializing HttpServer...");
    
    // 调用完整的初始化方法
    if (!initializeWithoutChannelRegistration()) {
        LOG_ERROR("Failed to initialize HttpServer without channel registration");
        return false;
    }
    
    // 如果有EventLoop，注册Channel
    if (event_loop_ && accept_channel_) {
        try {
            event_loop_->addChannel(accept_channel_.get());
            LOG_DEBUG("Accept channel registered with EventLoop");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to register accept channel: " + std::string(e.what()));
            return false;
        }
    }
    
    LOG_INFO("HttpServer initialization completed");
    return true;
}
```

**结果**: ✅ 链接错误修复完成，五子棋服务现在可以正常编译和运行

---

## 📊 **数据库表结构详细分析**

### **🎮 核心游戏表**

#### **1. gomoku_users（用户游戏统计表）**
**作用**: 本地缓存表，存储用户的五子棋游戏统计信息
**设计特点**: 
- 避免频繁跨服务调用
- 异步同步用户基础信息
- 专注五子棋相关统计

**字段分析**:
```sql
CREATE TABLE `gomoku_users` (
    `user_id` VARCHAR(64) PRIMARY KEY,          -- 用户ID（从用户服务同步）
    `username` VARCHAR(50) NOT NULL,            -- 用户名（缓存）
    `nickname` VARCHAR(100),                    -- 昵称（缓存）
    
    -- 🎯 五子棋专属统计
    `current_rating` INT DEFAULT 1500,          -- 当前积分（ELO算法）
    `peak_rating` INT DEFAULT 1500,             -- 历史最高积分
    `total_games` INT DEFAULT 0,                -- 总游戏局数
    `wins` INT DEFAULT 0,                       -- 胜局数
    `losses` INT DEFAULT 0,                     -- 败局数
    `draws` INT DEFAULT 0,                      -- 平局数
    `total_playtime_minutes` INT DEFAULT 0,     -- 总游戏时长
    
    -- 📊 状态追踪
    `last_game_at` TIMESTAMP NULL,              -- 最后游戏时间
    `is_active` BOOLEAN DEFAULT TRUE,           -- 是否活跃
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
```

**与其他表的关系**:
- **主表地位**: 其他表通过`user_id`关联
- **应用层维护**: 不使用外键，避免跨服务依赖
- **数据同步**: 通过消息队列从用户服务同步基础信息

#### **2. gomoku_rooms（游戏房间表）**
**作用**: 管理五子棋游戏房间的生命周期和配置
**设计特点**:
- 支持多种游戏模式（自由、连珠、换手等）
- JSON配置存储，灵活扩展
- 乐观锁防并发问题

**字段分析**:
```sql
CREATE TABLE `gomoku_rooms` (
    `room_id` VARCHAR(64) PRIMARY KEY,          -- 房间唯一标识
    `room_name` VARCHAR(100),                   -- 房间名称
    `creator_id` VARCHAR(64) NOT NULL,          -- 创建者ID
    `game_mode` ENUM('freestyle', 'renju', 'swap2', 'pro', 'tournament'),
    `room_type` ENUM('casual', 'ranked', 'tournament', 'private'),
    `is_private` BOOLEAN DEFAULT FALSE,         -- 私人房间标志
    `password_hash` VARCHAR(255),               -- 房间密码（加密）
    `max_spectators` INT DEFAULT 50,            -- 最大观战人数
    `allow_spectators` BOOLEAN DEFAULT TRUE,    -- 是否允许观战
    
    -- 🎮 游戏配置（JSON格式）
    `game_config` JSON NOT NULL,                -- 时限、规则等配置
    
    -- 📊 状态管理
    `room_state` ENUM('waiting', 'playing', 'paused', 'finished', 'abandoned'),
    `player_count` TINYINT UNSIGNED DEFAULT 0,  -- 当前玩家数
    `version` INT UNSIGNED DEFAULT 1,           -- 乐观锁版本号
    
    -- ⏰ 时间戳
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `started_at` TIMESTAMP NULL,                -- 游戏开始时间
    `finished_at` TIMESTAMP NULL,               -- 游戏结束时间
    `last_activity_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
```

**关系特点**:
- **创建者关联**: `creator_id` → `gomoku_users.user_id`（应用层维护）
- **房间→游戏**: 一个房间可以产生多个游戏（重开局）
- **乐观锁**: `version`字段防止并发修改冲突

#### **3. gomoku_games（游戏记录表）**
**作用**: 记录每局五子棋游戏的完整信息和结果
**设计特点**:
- 专注游戏数据，移除积分计算
- 支持多种游戏结果类型
- JSON存储棋盘状态和获胜线路

**字段分析**:
```sql
CREATE TABLE `gomoku_games` (
    `game_id` BIGINT PRIMARY KEY AUTO_INCREMENT, -- 游戏唯一ID
    `room_id` VARCHAR(64) NOT NULL,              -- 关联房间
    `black_player_id` VARCHAR(64) NOT NULL,      -- 黑方玩家
    `white_player_id` VARCHAR(64) NOT NULL,      -- 白方玩家
    `game_mode` ENUM('freestyle', 'renju', 'swap2', 'pro', 'tournament'),
    `game_type` ENUM('casual', 'ranked', 'tournament', 'friendly'),
    
    -- 🏆 游戏结果
    `game_result` ENUM('black_win', 'white_win', 'draw', 
                      'black_timeout', 'white_timeout', 
                      'black_surrender', 'white_surrender', 'abandoned'),
    `winner_id` VARCHAR(64) NULL,                -- 获胜者ID
    `win_type` ENUM('five_in_row', 'timeout', 'surrender', 'draw', 'abandoned'),
    `winning_line` JSON NULL,                    -- 获胜线路坐标
    
    -- 📊 游戏统计
    `total_moves` INT UNSIGNED DEFAULT 0,        -- 总步数
    `game_duration_seconds` INT UNSIGNED DEFAULT 0, -- 游戏时长
    `black_time_used_seconds` INT UNSIGNED DEFAULT 0, -- 黑方用时
    `white_time_used_seconds` INT UNSIGNED DEFAULT 0, -- 白方用时
    
    -- 💾 棋盘快照
    `final_board_state` JSON NULL,               -- 最终棋盘状态
    
    -- ⏰ 时间记录
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `started_at` TIMESTAMP NULL,
    `finished_at` TIMESTAMP NULL,
    
    -- 🖥️ 服务器信息
    `server_id` VARCHAR(64),                     -- 处理服务器ID
    `server_version` VARCHAR(20) DEFAULT '2.0.0' -- 服务器版本
);
```

**设计优化**:
- **移除积分字段**: 积分计算委托给游戏数据服务
- **JSON存储**: `winning_line`和`final_board_state`使用JSON格式
- **多种结果**: 支持正常结束、超时、投降等多种情况

#### **4. gomoku_moves（移动记录表）**
**作用**: 记录游戏中每一步的详细信息，支持复盘和分析
**设计特点**:
- 位置编码优化存储
- 毫秒精度时间戳
- 禁手检测支持

**字段分析**:
```sql
CREATE TABLE `gomoku_moves` (
    `move_id` BIGINT PRIMARY KEY AUTO_INCREMENT,  -- 移动唯一ID
    `game_id` BIGINT NOT NULL,                    -- 关联游戏
    `move_number` SMALLINT UNSIGNED NOT NULL,     -- 步数序号(1-225)
    `player_id` VARCHAR(64) NOT NULL,             -- 落子玩家
    `piece_type` ENUM('black', 'white') NOT NULL, -- 棋子颜色
    `position` SMALLINT UNSIGNED NOT NULL,        -- 位置编码(row*15+col)
    
    -- ⏱️ 时间信息
    `move_timestamp` TIMESTAMP(3) DEFAULT CURRENT_TIMESTAMP(3), -- 毫秒精度
    `time_used_ms` INT UNSIGNED DEFAULT 0,        -- 思考时间(毫秒)
    `remaining_time_seconds` INT DEFAULT 0,       -- 剩余时间
    
    -- 🚫 禁手检测（连珠模式）
    `is_forbidden` BOOLEAN DEFAULT FALSE,         -- 是否禁手
    `forbidden_type` ENUM('three_three', 'four_four', 'overline'), -- 禁手类型
    
    -- 🔗 约束
    FOREIGN KEY (`game_id`) REFERENCES `gomoku_games`(`game_id`) ON DELETE CASCADE,
    UNIQUE KEY uk_game_move (`game_id`, `move_number`) -- 确保步数唯一
);
```

**存储优化**:
- **位置编码**: `position = row * 15 + col`，节省存储空间
- **时间精度**: 毫秒级时间戳支持精确计时
- **禁手支持**: 连珠规则的三三、四四、长连禁手

### **🎭 社交功能表**

#### **5. gomoku_spectators（观战记录表）**
**作用**: 记录用户观战游戏的行为和时长
```sql
CREATE TABLE `gomoku_spectators` (
    `id` BIGINT PRIMARY KEY AUTO_INCREMENT,
    `game_id` BIGINT NOT NULL,                    -- 观战的游戏
    `user_id` VARCHAR(64) NOT NULL,               -- 观战用户
    `join_time` TIMESTAMP DEFAULT CURRENT_TIMESTAMP, -- 加入时间
    `leave_time` TIMESTAMP NULL,                  -- 离开时间
    `watch_duration_seconds` INT UNSIGNED DEFAULT 0, -- 观战时长
    
    FOREIGN KEY (`game_id`) REFERENCES `gomoku_games`(`game_id`) ON DELETE CASCADE
);
```

#### **6. gomoku_chat_messages（聊天记录表）**
**作用**: 存储房间内的聊天消息
```sql
CREATE TABLE `gomoku_chat_messages` (
    `message_id` BIGINT PRIMARY KEY AUTO_INCREMENT,
    `room_id` VARCHAR(64) NOT NULL,               -- 关联房间
    `user_id` VARCHAR(64) NOT NULL,               -- 发送者
    `message_type` ENUM('room', 'system') DEFAULT 'room', -- 消息类型
    `content` VARCHAR(500) NOT NULL,              -- 消息内容（限制长度）
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### **⚙️ 配置和监控表**

#### **7. gomoku_configs（游戏配置表）**
**作用**: 存储游戏的各种配置参数，支持热更新
```sql
CREATE TABLE `gomoku_configs` (
    `config_id` BIGINT PRIMARY KEY AUTO_INCREMENT,
    `config_name` VARCHAR(50) NOT NULL UNIQUE,    -- 配置名称
    `config_data` JSON NOT NULL,                  -- 配置数据（JSON）
    `is_active` BOOLEAN DEFAULT TRUE,             -- 是否启用
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
```

**配置类型**:
- `freestyle_default`: 自由五子棋默认配置
- `renju_default`: 连珠模式默认配置
- `blitz_mode`: 快棋模式配置
- `tournament_mode`: 比赛模式配置
- `thread_pool_config`: 线程池配置

#### **8. gomoku_server_status（服务器状态表）**
**作用**: 监控服务器运行状态和性能指标
```sql
CREATE TABLE `gomoku_server_status` (
    `id` BIGINT PRIMARY KEY AUTO_INCREMENT,
    `server_id` VARCHAR(64) NOT NULL,             -- 服务器标识
    `status` ENUM('starting', 'running', 'stopping', 'stopped', 'error'),
    `active_rooms` INT UNSIGNED DEFAULT 0,        -- 活跃房间数
    `active_games` INT UNSIGNED DEFAULT 0,        -- 活跃游戏数
    `connected_players` INT UNSIGNED DEFAULT 0,   -- 连接玩家数
    `memory_usage_mb` INT UNSIGNED DEFAULT 0,     -- 内存使用量
    `uptime_seconds` INT UNSIGNED DEFAULT 0,      -- 运行时间
    
    -- 🔧 线程池监控
    `thread_pool_size` INT UNSIGNED DEFAULT 0,    -- 线程池大小
    `active_threads` INT UNSIGNED DEFAULT 0,      -- 活跃线程数
    `queue_size` INT UNSIGNED DEFAULT 0,          -- 任务队列大小
    `recorded_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

---

## 🌐 **微服务数据流架构图**

### **整体服务架构**
```mermaid
graph TB
    subgraph "客户端层"
        WebClient[Web客户端]
        MobileClient[移动客户端]
        DesktopClient[桌面客户端]
    end
    
    subgraph "API网关层"
        Gateway[API网关<br/>认证+路由+限流]
    end
    
    subgraph "核心服务层"
        AuthService[认证服务<br/>:8081]
        UserService[用户服务<br/>:8082] 
        GameDataService[游戏数据服务<br/>:8083]
        GomokuService[五子棋游戏服务<br/>:8084]
    end
    
    subgraph "数据存储层"
        AuthDB[(认证数据库<br/>auth_sessions_db)]
        UserDB[(用户数据库<br/>user_service_db)]
        GameDB[(游戏数据库<br/>game_service_db)]
        GomokuDB[(五子棋数据库<br/>gomoku_game_db)]
        Redis[(Redis缓存集群)]
    end
    
    subgraph "消息队列层"
        MessageQueue[消息队列<br/>RabbitMQ/Kafka]
    end
    
    %% 客户端连接
    WebClient --> Gateway
    MobileClient --> Gateway
    DesktopClient --> Gateway
    
    %% API网关路由
    Gateway --> AuthService
    Gateway --> UserService
    Gateway --> GameDataService
    Gateway --> GomokuService
    
    %% 服务间通信
    GomokuService -.->|HTTP调用| UserService
    GomokuService -.->|HTTP调用| GameDataService
    UserService -.->|事件发布| MessageQueue
    GameDataService -.->|事件发布| MessageQueue
    MessageQueue -.->|事件消费| GomokuService
    
    %% 数据存储连接
    AuthService --> AuthDB
    AuthService --> Redis
    UserService --> UserDB
    UserService --> Redis
    GameDataService --> GameDB
    GameDataService --> Redis
    GomokuService --> GomokuDB
    GomokuService --> Redis
    
    %% 样式定义
    classDef serviceClass fill:#e1f5fe,stroke:#01579b,stroke-width:2px
    classDef dbClass fill:#f3e5f5,stroke:#4a148c,stroke-width:2px
    classDef clientClass fill:#e8f5e8,stroke:#1b5e20,stroke-width:2px
    classDef gatewayClass fill:#fff3e0,stroke:#e65100,stroke-width:2px
    
    class AuthService,UserService,GameDataService,GomokuService serviceClass
    class AuthDB,UserDB,GameDB,GomokuDB,Redis dbClass
    class WebClient,MobileClient,DesktopClient clientClass
    class Gateway,MessageQueue gatewayClass
```

### **五子棋服务数据流详情**
```mermaid
sequenceDiagram
    participant Client as 游戏客户端
    participant Gateway as API网关
    participant Gomoku as 五子棋服务
    participant User as 用户服务
    participant GameData as 游戏数据服务
    participant GomokuDB as 五子棋数据库
    participant MQ as 消息队列
    participant Redis as Redis缓存
    
    %% 用户认证和进入游戏
    Client->>Gateway: 1. 请求进入游戏 + JWT Token
    Gateway->>Gateway: 2. 验证JWT Token
    Gateway->>Gomoku: 3. 转发请求到五子棋服务
    
    %% 用户信息获取和缓存
    Gomoku->>User: 4. HTTP: GET /api/users/{user_id}
    User-->>Gomoku: 5. 返回用户基础信息
    Gomoku->>GameData: 6. HTTP: GET /api/game-profiles/{user_id}?game_type=1
    GameData-->>Gomoku: 7. 返回五子棋游戏档案
    
    %% 本地缓存更新
    Gomoku->>GomokuDB: 8. UPSERT gomoku_users 表
    Gomoku->>Redis: 9. 缓存用户会话信息
    
    %% 房间创建和游戏开始
    Client->>Gomoku: 10. WebSocket: 创建/加入房间
    Gomoku->>GomokuDB: 11. INSERT INTO gomoku_rooms
    Gomoku->>GomokuDB: 12. INSERT INTO gomoku_games
    
    %% 游戏进行中的数据流
    loop 游戏进行中
        Client->>Gomoku: 13. WebSocket: 落子消息
        Gomoku->>GomokuDB: 14. INSERT INTO gomoku_moves
        Gomoku->>Redis: 15. 更新游戏状态缓存
        Gomoku-->>Client: 16. WebSocket: 广播落子结果
    end
    
    %% 游戏结束和数据同步
    Gomoku->>GomokuDB: 17. UPDATE gomoku_games SET game_result
    Gomoku->>GameData: 18. HTTP: POST /api/game-results
    GameData-->>Gomoku: 19. 返回积分变化
    Gomoku->>GomokuDB: 20. UPDATE gomoku_users 统计信息
    
    %% 异步事件发布
    Gomoku->>MQ: 21. 发布游戏结束事件
    GameData->>MQ: 22. 发布积分变化事件
    User->>MQ: 23. 发布用户状态变化事件
    
    %% 事件消费和数据同步
    MQ-->>Gomoku: 24. 消费用户状态变化事件
    Gomoku->>GomokuDB: 25. 同步更新 gomoku_users
    
    Note over Client,Redis: 🎯 数据流特点：<br/>1. 用户信息通过HTTP同步获取<br/>2. 游戏状态通过WebSocket实时更新<br/>3. 统计数据通过消息队列异步同步<br/>4. Redis缓存提高性能
```

### **数据库表关系ER图（完整版）**
```mermaid
erDiagram
    %% 用户相关表
    GOMOKU_USERS {
        VARCHAR user_id PK "用户ID(本地缓存)"
        VARCHAR username "用户名"
        VARCHAR nickname "昵称"
        INT current_rating "当前积分"
        INT peak_rating "历史最高积分"
        INT total_games "总游戏数"
        INT wins "胜局数"
        INT losses "败局数"
        INT draws "平局数"
        INT total_playtime_minutes "总游戏时长"
        TIMESTAMP last_game_at "最后游戏时间"
        BOOLEAN is_active "是否活跃"
        TIMESTAMP created_at "创建时间"
        TIMESTAMP updated_at "更新时间"
    }
    
    %% 房间和游戏表
    GOMOKU_ROOMS {
        VARCHAR room_id PK "房间ID"
        VARCHAR room_name "房间名称"
        VARCHAR creator_id FK "创建者ID"
        ENUM game_mode "游戏模式"
        ENUM room_type "房间类型"
        BOOLEAN is_private "是否私人房间"
        VARCHAR password_hash "房间密码哈希"
        INT max_spectators "最大观战人数"
        BOOLEAN allow_spectators "是否允许观战"
        JSON game_config "游戏配置"
        ENUM room_state "房间状态"
        TINYINT player_count "当前玩家数量"
        INT version "版本号(乐观锁)"
        TIMESTAMP created_at "创建时间"
        TIMESTAMP started_at "开始游戏时间"
        TIMESTAMP finished_at "结束时间"
        TIMESTAMP last_activity_at "最后活动时间"
    }
    
    GOMOKU_GAMES {
        BIGINT game_id PK "游戏ID"
        VARCHAR room_id FK "房间ID"
        VARCHAR black_player_id FK "黑方玩家ID"
        VARCHAR white_player_id FK "白方玩家ID"
        ENUM game_mode "游戏模式"
        ENUM game_type "游戏类型"
        ENUM game_result "游戏结果"
        VARCHAR winner_id FK "获胜者ID"
        ENUM win_type "获胜方式"
        JSON winning_line "获胜线路坐标"
        INT total_moves "总步数"
        INT game_duration_seconds "游戏持续时间"
        INT black_time_used_seconds "黑方用时"
        INT white_time_used_seconds "白方用时"
        JSON final_board_state "最终棋盘状态"
        TIMESTAMP created_at "创建时间"
        TIMESTAMP started_at "开始时间"
        TIMESTAMP finished_at "结束时间"
        VARCHAR server_id "服务器ID"
        VARCHAR server_version "服务器版本"
    }
    
    GOMOKU_MOVES {
        BIGINT move_id PK "移动ID"
        BIGINT game_id FK "游戏ID"
        SMALLINT move_number "步数序号"
        VARCHAR player_id FK "玩家ID"
        ENUM piece_type "棋子类型"
        SMALLINT position "位置编码"
        TIMESTAMP move_timestamp "落子时间戳"
        INT time_used_ms "本步思考时间"
        INT remaining_time_seconds "剩余时间"
        BOOLEAN is_forbidden "是否禁手"
        ENUM forbidden_type "禁手类型"
    }
    
    %% 社交功能表
    GOMOKU_SPECTATORS {
        BIGINT id PK "ID"
        BIGINT game_id FK "游戏ID"
        VARCHAR user_id FK "观战用户ID"
        TIMESTAMP join_time "加入观战时间"
        TIMESTAMP leave_time "离开观战时间"
        INT watch_duration_seconds "观战时长"
    }
    
    GOMOKU_CHAT_MESSAGES {
        BIGINT message_id PK "消息ID"
        VARCHAR room_id FK "房间ID"
        VARCHAR user_id FK "发送者ID"
        ENUM message_type "消息类型"
        VARCHAR content "消息内容"
        TIMESTAMP created_at "发送时间"
    }
    
    %% 配置和监控表
    GOMOKU_CONFIGS {
        BIGINT config_id PK "配置ID"
        VARCHAR config_name UK "配置名称"
        JSON config_data "配置数据"
        BOOLEAN is_active "是否启用"
        TIMESTAMP created_at "创建时间"
        TIMESTAMP updated_at "更新时间"
    }
    
    GOMOKU_SERVER_STATUS {
        BIGINT id PK "ID"
        VARCHAR server_id "服务器ID"
        ENUM status "服务器状态"
        INT active_rooms "活跃房间数"
        INT active_games "活跃游戏数"
        INT connected_players "连接玩家数"
        INT memory_usage_mb "内存使用量"
        INT uptime_seconds "运行时间"
        INT thread_pool_size "线程池大小"
        INT active_threads "活跃线程数"
        INT queue_size "任务队列大小"
        TIMESTAMP recorded_at "记录时间"
    }
    
    %% 关系定义（强约束-本地数据库内）
    GOMOKU_GAMES ||--o{ GOMOKU_MOVES : "游戏包含多个移动"
    GOMOKU_GAMES ||--o{ GOMOKU_SPECTATORS : "游戏可有多个观战者"
    
    %% 关系定义（弱约束-应用层维护）
    GOMOKU_USERS ||--o{ GOMOKU_ROOMS : "用户创建房间"
    GOMOKU_USERS ||--o{ GOMOKU_GAMES : "用户参与游戏(黑白方)"
    GOMOKU_USERS ||--o{ GOMOKU_MOVES : "用户落子"
    GOMOKU_USERS ||--o{ GOMOKU_SPECTATORS : "用户观战"
    GOMOKU_USERS ||--o{ GOMOKU_CHAT_MESSAGES : "用户聊天"
    GOMOKU_ROOMS ||--o| GOMOKU_GAMES : "房间产生游戏"
    GOMOKU_ROOMS ||--o{ GOMOKU_CHAT_MESSAGES : "房间包含聊天消息"
```

---

## 📈 **数据流量和性能特点**

### **读写分离策略**
| 操作类型 | 主要表 | 频率 | 缓存策略 |
|----------|--------|------|----------|
| **高频读** | gomoku_users, gomoku_rooms | 极高 | Redis缓存 + 本地缓存 |
| **高频写** | gomoku_moves, gomoku_server_status | 极高 | 批量插入 + 异步写入 |
| **中频读写** | gomoku_games, gomoku_spectators | 中等 | Redis缓存 |
| **低频读写** | gomoku_configs, gomoku_chat_messages | 较低 | 数据库直接访问 |

### **数据同步策略**
```mermaid
graph TB
    subgraph "数据同步流"
        UserService[用户服务] -->|用户信息变更事件| MQ[消息队列]
        GameDataService[游戏数据服务] -->|积分变更事件| MQ
        MQ -->|事件消费| GomokuService[五子棋服务]
        GomokuService -->|更新本地缓存| GomokuDB[(gomoku_users表)]
    end
    
    subgraph "数据一致性保证"
        GomokuService -->|定期同步检查| UserService
        GomokuService -->|版本比较| GameDataService
        GomokuService -->|数据修复| GomokuDB
    end
    
    subgraph "性能优化"
        GomokuService -->|热点数据| RedisCluster[Redis集群]
        GomokuService -->|批量操作| BatchWrite[批量写入]
        GomokuService -->|读副本| ReadReplica[只读副本]
    end
```

---

## 🎯 **总结与优势**

### **🏗️ 架构优势**
1. **微服务解耦**: 每个服务管理自己的数据域
2. **性能优化**: 本地缓存 + Redis + 批量操作
3. **扩展性**: JSON配置支持灵活扩展
4. **一致性**: 事件驱动的最终一致性
5. **监控完备**: 完整的性能和状态监控

### **📊 数据特点**
1. **用户数据**: 跨服务同步，本地缓存
2. **游戏数据**: 本地存储，实时更新
3. **配置数据**: 热更新支持
4. **监控数据**: 定期清理，保持轻量

### **🚀 性能预期**
- **并发支持**: 10,000+ 同时在线玩家
- **响应时间**: WebSocket < 10ms, HTTP < 100ms
- **数据吞吐**: 100,000+ 移动记录/小时
- **存储效率**: 位置编码节省60%存储空间

---

**🎉 结论: 五子棋游戏服务采用了现代微服务架构，具备高性能、高可用、易扩展的特点，完全满足大规模在线游戏的需求！**
