# 游戏数据核心服务API文档

## 概述

本文档详细描述游戏数据核心服务(Game Data Service)的HTTP REST API，基于实际代码实现，确保信息的准确性和完整性。游戏数据服务专注于游戏相关数据管理，包括用户游戏档案、成就系统、库存管理、货币系统和排行榜等核心功能。

---

## 游戏数据核心服务 (GameDataService)

### 基础信息
- **服务名称**: `game_data_service`
- **默认端口**: `8083` (HTTP)
- **服务版本**: `1.0.0`
- **基础路径**: `/api/v1/gamedata/`
- **数据库**: MySQL (game_service_db) + Redis (database 3/4, 缓存)
- **架构特点**: 微服务架构、数据分层存储、时间轮调度、线程池优化

---

## **👤 1. 用户游戏档案API**

### **1.1 创建用户游戏档案**

**端点**: `POST /api/v1/gamedata/profiles`

**功能**: 创建新的用户游戏档案

**请求头**:
```http
Content-Type: application/json
```

**请求JSON格式**:
```json
{
  "user_id": "user_12345",           // 必填：用户ID
  "display_name": "GamerNinja",      // 必填：游戏显示名称
  "level": 1,                       // 可选：用户等级，默认1
  "experience_points": 0,           // 可选：经验值，默认0
  "avatar_url": "https://example.com/avatar.jpg" // 可选：头像URL
}
```

**成功响应** (201):
```json
{
  "success": true,
  "message": "User profile created successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "display_name": "GamerNinja",
    "level": 1,
    "experience_points": 0,
    "avatar_url": "https://example.com/avatar.jpg",
    "created_at": 1642145823456,
    "last_login_at": 1642145823456
  }
}
```

**失败响应** (400):
```json
{
  "success": false,
  "message": "Missing required field: user_id",
  "service": "game_data_service",
  "timestamp": 1642145823456
}
```

**失败响应** (500):
```json
{
  "success": false,
  "message": "Failed to create user profile",
  "service": "game_data_service",
  "timestamp": 1642145823456
}
```

---

### **1.2 获取用户游戏档案**

**端点**: `GET /api/v1/gamedata/profiles/{user_id}`

**功能**: 根据用户ID获取游戏档案信息

**路径参数**:
- `user_id`: 用户ID

**成功响应** (200):
```json
{
  "success": true,
  "message": "User profile retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "display_name": "GamerNinja",
    "level": 15,
    "experience_points": 2500,
    "avatar_url": "https://example.com/avatar.jpg",
    "created_at": 1642145823456,
    "last_login_at": 1642150000000
  }
}
```

**失败响应** (404):
```json
{
  "success": false,
  "message": "User profile not found",
  "service": "game_data_service",
  "timestamp": 1642145823456
}
```

**失败响应** (400):
```json
{
  "success": false,
  "message": "Missing user_id parameter",
  "service": "game_data_service",
  "timestamp": 1642145823456
}
```

---

### **1.3 更新用户游戏档案**

**端点**: `PUT /api/v1/gamedata/profiles/{user_id}`

**功能**: 更新用户游戏档案信息

**路径参数**:
- `user_id`: 用户ID

**请求JSON格式**:
```json
{
  "display_name": "ProGamer2023",    // 可选：新的显示名称
  "level": 20,                      // 可选：新的等级
  "experience_points": 5000,        // 可选：新的经验值
  "avatar_url": "https://example.com/new_avatar.jpg" // 可选：新的头像URL
}
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "User profile updated successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "display_name": "ProGamer2023",
    "level": 20,
    "experience_points": 5000,
    "avatar_url": "https://example.com/new_avatar.jpg",
    "created_at": 1642145823456,
    "last_login_at": 1642150000000
  }
}
```

---

### **1.4 删除用户游戏档案**

**端点**: `DELETE /api/v1/gamedata/profiles/{user_id}`

**功能**: 删除用户游戏档案

**路径参数**:
- `user_id`: 用户ID

**成功响应** (200):
```json
{
  "success": true,
  "message": "User profile deleted successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456
}
```

---

### **1.5 获取用户档案列表**

**端点**: `GET /api/v1/gamedata/profiles`

**功能**: 获取用户档案列表(支持分页和筛选)

**查询参数**:
```
page=1              // 页码，默认1
limit=20            // 每页数量，默认20，最大100
sort_by=level       // 排序字段，默认created_at
sort_order=desc     // 排序顺序，asc/desc，默认desc
search=Pro          // 搜索显示名称，可选
level_min=1         // 最小等级筛选，可选
level_max=50        // 最大等级筛选，可选
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "User profiles retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "items": [
      {
        "user_id": "user_12345",
        "display_name": "ProGamer2023",
        "level": 20,
        "experience_points": 5000,
        "avatar_url": "https://example.com/avatar1.jpg",
        "created_at": 1642145823456,
        "last_login_at": 1642150000000
      },
      {
        "user_id": "user_67890",
        "display_name": "GameMaster",
        "level": 35,
        "experience_points": 12000,
        "avatar_url": "https://example.com/avatar2.jpg",
        "created_at": 1642140000000,
        "last_login_at": 1642149000000
      }
    ],
    "total_count": 156,
    "page": 1,
    "limit": 20,
    "total_pages": 8
  }
}
```

---

## **🏆 2. 成就系统API**

### **2.0 成就系统数据模型**

#### **成就类型 (AchievementType)**
| 类型 | 值 | 描述 |
|------|------|------|
| MILESTONE | 0 | 里程碑成就（一次性触发） |
| PROGRESS | 1 | 进度型成就（累积进度触发） |
| HIDDEN | 2 | 隐藏成就（特殊条件触发，解锁前不显示） |
| SOCIAL | 3 | 社交成就 |

#### **成就稀有度 (AchievementRarity)**
| 稀有度 | 值 | 颜色 | 描述 |
|------|------|------|------|
| COMMON | 0 | 灰色 | 普通成就 |
| RARE | 1 | 蓝色 | 稀有成就 |
| EPIC | 2 | 紫色 | 史诗成就 |
| LEGENDARY | 3 | 橙色 | 传说成就 |

#### **条件表达式语法**
成就解锁条件使用简单的条件表达式语言：

| 操作符 | 描述 | 示例 |
|--------|------|------|
| `>=` | 大于等于 | `total_wins >= 10` |
| `<=` | 小于等于 | `rating <= 500` |
| `==` | 等于 | `last_game_result == 1` |
| `!=` | 不等于 | `game_mode != 0` |
| `>` | 大于 | `current_win_streak > 5` |
| `<` | 小于 | `last_game_moves < 30` |
| `AND` | 逻辑与 | `total_wins >= 10 AND current_rating >= 1500` |
| `OR` | 逻辑或 | `rating >= 2000 OR win_streak >= 10` |

**可用上下文字段**:
| 字段 | 类型 | 描述 |
|------|------|------|
| `total_games` | int | 总对局数 |
| `total_wins` | int | 总胜利数 |
| `total_losses` | int | 总失败数 |
| `total_draws` | int | 总平局数 |
| `current_win_streak` | int | 当前连胜数 |
| `best_win_streak` | int | 最佳连胜数 |
| `current_rating` | int | 当前评分 |
| `peak_rating` | int | 最高评分 |
| `level` | int | 用户等级 |
| `last_game_result` | int | 上局结果 (0=LOSS, 1=WIN, 2=DRAW) |
| `last_game_moves` | int | 上局步数 |
| `last_game_duration` | int | 上局时长(秒) |
| `opponent_rating` | int | 对手评分 |
| `was_underdog` | int | 是否处于劣势 (0/1) |

---

### **2.1 解锁成就**

**端点**: `POST /api/v1/gamedata/achievements`

**功能**: 为用户解锁新成就

**请求JSON格式**:
```json
{
  "user_id": "user_12345",                // 必填：用户ID
  "achievement_type": "first_win",        // 必填：成就类型
  "title": "First Victory",              // 必填：成就标题
  "description": "Win your first game",  // 必填：成就描述
  "points": 100                          // 必填：成就奖励积分
}
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "Achievement unlocked successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "achievement_id": "ach_12345",
    "user_id": "user_12345",
    "achievement_type": "first_win",
    "title": "First Victory",
    "description": "Win your first game",
    "points": 100,
    "unlocked_at": 1642145823456
  }
}
```

---

### **2.2 获取用户成就**

**端点**: `GET /api/v1/gamedata/achievements/{user_id}`

**功能**: 获取用户所有已解锁的成就

**路径参数**:
- `user_id`: 用户ID

**查询参数**:
```
achievement_type=first_win  // 可选：按成就类型筛选
sort_by=unlocked_at        // 排序字段，默认unlocked_at
sort_order=desc            // 排序顺序，默认desc
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "User achievements retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "total_achievements": 5,
    "total_points": 650,
    "achievements": [
      {
        "achievement_id": "ach_12345",
        "achievement_type": "first_win",
        "title": "First Victory",
        "description": "Win your first game",
        "points": 100,
        "unlocked_at": 1642145823456
      },
      {
        "achievement_id": "ach_12346",
        "achievement_type": "streak_master",
        "title": "Winning Streak",
        "description": "Win 5 games in a row",
        "points": 250,
        "unlocked_at": 1642146000000
      }
    ]
  }
}
```

---

### **2.3 获取成就定义列表** (新增)

**端点**: `GET /api/v1/gamedata/achievements/definitions`

**功能**: 获取所有可用的成就定义

**查询参数**:
```
type=milestone              // 可选：按成就类型筛选 (milestone/progress/hidden/social)
rarity=rare                // 可选：按稀有度筛选 (common/rare/epic/legendary)
game_type=gomoku           // 可选：按游戏类型筛选
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "Achievement definitions retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "total": 25,
    "definitions": [
      {
        "id": "first_win",
        "name": "初出茅庐",
        "description": "赢得第一场游戏",
        "type": "milestone",
        "rarity": "common",
        "points": 10,
        "condition_expr": "total_wins >= 1",
        "target_progress": 1,
        "is_hidden": false,
        "game_type_restriction": "",
        "rewards": {
          "gold": 100,
          "gems": 0,
          "honor": 0,
          "experience": 10
        }
      },
      {
        "id": "win_streak_5",
        "name": "势如破竹",
        "description": "连续赢得5场游戏",
        "type": "progress",
        "rarity": "rare",
        "points": 30,
        "condition_expr": "current_win_streak >= 5",
        "target_progress": 5,
        "is_hidden": false,
        "game_type_restriction": "",
        "rewards": {
          "gold": 200,
          "gems": 0,
          "honor": 10,
          "experience": 30
        }
      },
      {
        "id": "perfect_game",
        "name": "完美对局",
        "description": "在30步内获胜",
        "type": "hidden",
        "rarity": "legendary",
        "points": 100,
        "condition_expr": "last_game_moves <= 30 AND last_game_result == 1",
        "target_progress": 1,
        "is_hidden": true,
        "game_type_restriction": "gomoku",
        "rewards": {
          "gold": 1000,
          "gems": 20,
          "honor": 100,
          "experience": 100
        }
      }
    ]
  }
}
```

---

### **2.4 获取单个成就定义** (新增)

**端点**: `GET /api/v1/gamedata/achievements/definitions/{achievement_id}`

**功能**: 获取指定成就的详细定义

**路径参数**:
- `achievement_id`: 成就ID

**成功响应** (200):
```json
{
  "success": true,
  "message": "Achievement definition retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "id": "win_100_games",
    "name": "棋道高手",
    "description": "累计赢得100场游戏",
    "type": "milestone",
    "rarity": "epic",
    "points": 50,
    "condition_expr": "total_wins >= 100",
    "target_progress": 100,
    "is_hidden": false,
    "game_type_restriction": "",
    "rewards": {
      "gold": 2000,
      "gems": 10,
      "honor": 50,
      "experience": 100
    }
  }
}
```

---

### **2.5 获取用户成就进度** (新增)

**端点**: `GET /api/v1/gamedata/achievements/{user_id}/progress/{achievement_id}`

**功能**: 获取用户对特定成就的进度

**路径参数**:
- `user_id`: 用户ID
- `achievement_id`: 成就ID

**成功响应** (200):
```json
{
  "success": true,
  "message": "Achievement progress retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "achievement_id": "win_100_games",
    "achievement_name": "棋道高手",
    "current_progress": 45,
    "target_progress": 100,
    "progress_percentage": 45,
    "is_unlocked": false,
    "type": "progress"
  }
}
```

---

### **2.6 获取用户成就点数** (新增)

**端点**: `GET /api/v1/gamedata/achievements/{user_id}/points`

**功能**: 获取用户的总成就点数

**路径参数**:
- `user_id`: 用户ID

**成功响应** (200):
```json
{
  "success": true,
  "message": "Achievement points retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "total_points": 650,
    "achievements_count": 15,
    "by_rarity": {
      "common": {"count": 8, "points": 100},
      "rare": {"count": 4, "points": 200},
      "epic": {"count": 2, "points": 200},
      "legendary": {"count": 1, "points": 150}
    }
  }
}
```

---

### **2.7 获取成就系统统计** (新增)

**端点**: `GET /api/v1/gamedata/achievements/statistics`

**功能**: 获取成就系统的整体统计信息

**成功响应** (200):
```json
{
  "success": true,
  "message": "Achievement statistics retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "total_definitions": 25,
    "enabled": true,
    "definitions_loaded": true,
    "by_type": {
      "milestone": 10,
      "progress": 8,
      "hidden": 5,
      "social": 2
    },
    "by_rarity": {
      "common": 10,
      "rare": 8,
      "epic": 5,
      "legendary": 2
    }
  }
}
```

---

## **🎒 3. 库存管理API**

### **3.1 添加库存物品**

**端点**: `POST /api/v1/gamedata/inventory`

**功能**: 向用户库存添加物品

**请求JSON格式**:
```json
{
  "user_id": "user_12345",              // 必填：用户ID
  "item_type": "powerup",               // 必填：物品类型
  "item_name": "Speed Boost",           // 必填：物品名称
  "quantity": 3,                        // 必填：数量
  "metadata": {                         // 可选：物品属性
    "duration": "30",
    "effect": "speed_increase",
    "rarity": "common"
  }
}
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "Inventory item added successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "item_id": "item_12345",
    "user_id": "user_12345",
    "item_type": "powerup",
    "item_name": "Speed Boost",
    "quantity": 3,
    "metadata": {
      "duration": "30",
      "effect": "speed_increase",
      "rarity": "common"
    },
    "acquired_at": 1642145823456
  }
}
```

---

### **3.2 获取用户库存**

**端点**: `GET /api/v1/gamedata/inventory/{user_id}`

**功能**: 获取用户的库存物品列表

**路径参数**:
- `user_id`: 用户ID

**查询参数**:
```
item_type=powerup    // 可选：按物品类型筛选
sort_by=acquired_at  // 排序字段，默认acquired_at
sort_order=desc      // 排序顺序，默认desc
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "User inventory retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "total_items": 8,
    "items": [
      {
        "item_id": "item_12345",
        "item_type": "powerup",
        "item_name": "Speed Boost",
        "quantity": 3,
        "metadata": {
          "duration": "30",
          "effect": "speed_increase",
          "rarity": "common"
        },
        "acquired_at": 1642145823456
      },
      {
        "item_id": "item_12346",
        "item_type": "skin",
        "item_name": "Golden Snake",
        "quantity": 1,
        "metadata": {
          "color": "gold",
          "rarity": "legendary"
        },
        "acquired_at": 1642140000000
      }
    ]
  }
}
```

---

### **3.3 使用库存物品**

**端点**: `POST /api/v1/gamedata/inventory/{user_id}/use`

**功能**: 使用库存中的物品

**路径参数**:
- `user_id`: 用户ID

**请求JSON格式**:
```json
{
  "item_id": "item_12345",    // 必填：物品ID
  "quantity": 1               // 必填：使用数量
}
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "Inventory item used successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "item_id": "item_12345",
    "user_id": "user_12345",
    "quantity_used": 1,
    "remaining_quantity": 2,
    "effect_applied": true
  }
}
```

---

## **💰 4. 货币系统API**

### **4.1 获取用户特定货币**

**端点**: `GET /api/v1/gamedata/currency/{user_id}/{currency_type}`

**功能**: 获取用户特定类型的货币数量

**路径参数**:
- `user_id`: 用户ID
- `currency_type`: 货币类型 (coins/gems/tokens等)

**成功响应** (200):
```json
{
  "success": true,
  "message": "User currency retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "currency_type": "coins",
    "amount": 2500,
    "last_updated_at": 1642145000000
  }
}
```

---

### **4.2 更新用户货币**

**端点**: `PUT /api/v1/gamedata/currency`

**功能**: 更新用户货币数量

**请求JSON格式**:
```json
{
  "user_id": "user_12345",      // 必填：用户ID
  "currency_type": "coins",     // 必填：货币类型
  "amount": 3000,               // 必填：新的货币数量
  "operation": "set"            // 必填：操作类型 set/add/subtract
}
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "User currency updated successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "currency_type": "coins",
    "previous_amount": 2500,
    "new_amount": 3000,
    "change_amount": 500,
    "last_updated_at": 1642145823456
  }
}
```

---

### **4.3 获取用户所有货币**

**端点**: `GET /api/v1/gamedata/currency/{user_id}`

**功能**: 获取用户所有类型的货币

**路径参数**:
- `user_id`: 用户ID

**成功响应** (200):
```json
{
  "success": true,
  "message": "All user currencies retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "currencies": [
      {
        "currency_type": "coins",
        "amount": 3000,
        "last_updated_at": 1642145823456
      },
      {
        "currency_type": "gems",
        "amount": 150,
        "last_updated_at": 1642140000000
      },
      {
        "currency_type": "tokens",
        "amount": 25,
        "last_updated_at": 1642135000000
      }
    ],
    "total_currencies": 3
  }
}
```

---

## **🏅 5. 排行榜API**

### **5.0 排行榜数据模型**

#### **排行榜类型 (LeaderboardType)**
| 类型 | 值 | 描述 |
|------|------|------|
| RATING | rating | 评分排行榜 |
| WIN_STREAK | win_streak | 连胜排行榜 |
| PLAYTIME | playtime | 游戏时长排行榜（小时） |
| WIN_RATE | win_rate | 胜率排行榜 |
| EXPERIENCE | experience | 经验值排行榜 |
| ACHIEVEMENTS | achievements | 成就点数排行榜 |

#### **排行榜范围 (LeaderboardScope)**
| 范围 | 值 | 描述 |
|------|------|------|
| GLOBAL | global | 全球排行榜 |
| WEEKLY | weekly | 周排行榜（每周重置） |
| MONTHLY | monthly | 月排行榜（每月重置） |
| SEASONAL | seasonal | 赛季排行榜 |

---

### **5.1 更新排行榜**

**端点**: `POST /api/v1/gamedata/leaderboard`

**功能**: 更新用户在排行榜中的记录

**请求JSON格式**:
```json
{
  "user_id": "user_12345",           // 必填：用户ID
  "leaderboard_type": "global",      // 必填：排行榜类型 global/weekly/monthly
  "game_type": "gomoku",             // 必填：游戏类型
  "score": 1850,                     // 必填：分数
  "extra_data": {                    // 可选：额外数据
    "wins": 25,
    "losses": 5,
    "win_rate": 0.833
  }
}
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "Leaderboard updated successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "entry_id": "entry_12345",
    "user_id": "user_12345",
    "leaderboard_type": "global",
    "game_type": "gomoku",
    "score": 1850,
    "rank_position": 15,
    "extra_data": {
      "wins": 25,
      "losses": 5,
      "win_rate": 0.833
    },
    "recorded_at": 1642145823456
  }
}
```

---

### **5.2 获取排行榜**

**端点**: `GET /api/v1/gamedata/leaderboard/{leaderboard_type}/{game_type}`

**功能**: 获取指定类型和游戏的排行榜

**路径参数**:
- `leaderboard_type`: 排行榜类型 (global/weekly/monthly)
- `game_type`: 游戏类型 (gomoku/snake等)

**查询参数**:
```
limit=50            // 返回条目数，默认50，最大100
offset=0            // 偏移量，默认0
include_user=user_12345  // 可选：包含特定用户，即使不在前N名
type=rating         // 可选：排行榜数据类型 (rating/win_streak/playtime/win_rate/experience/achievements)
scope=global        // 可选：排行榜范围 (global/weekly/monthly/seasonal)
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "Leaderboard retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "type": "rating",
    "scope": "global",
    "updated_at": "2026-02-21T10:30:00Z",
    "total_players": 1250,
    "entries": [
      {
        "rank": 1,
        "user_id": "user_top1",
        "display_name": "ChessMaster",
        "avatar_url": "https://example.com/avatar1.jpg",
        "score": 2200,
        "tier_name": "Diamond I",
        "games_played": 160,
        "win_rate": 0.938
      },
      {
        "rank": 2,
        "user_id": "user_top2",
        "display_name": "ProGamer",
        "avatar_url": "https://example.com/avatar2.jpg",
        "score": 2150,
        "tier_name": "Diamond II",
        "games_played": 155,
        "win_rate": 0.903
      }
    ],
    "my_rank": {
      "rank": 15,
      "user_id": "user_12345",
      "display_name": "MyNickname",
      "score": 1850,
      "tier_name": "Platinum I"
    }
  }
}
```

---

### **5.3 获取用户排名**

**端点**: `GET /api/v1/gamedata/leaderboard/{leaderboard_type}/{game_type}/rank/{user_id}`

**功能**: 获取特定用户在排行榜中的排名

**路径参数**:
- `leaderboard_type`: 排行榜类型
- `game_type`: 游戏类型
- `user_id`: 用户ID

**成功响应** (200):
```json
{
  "success": true,
  "message": "User rank retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "user_id": "user_12345",
    "leaderboard_type": "global",
    "game_type": "gomoku",
    "rank_position": 15,
    "score": 1850,
    "total_players": 1250,
    "percentile": 98.8,
    "extra_data": {
      "wins": 25,
      "losses": 5,
      "win_rate": 0.833
    },
    "recorded_at": 1642145823456
  }
}
```

---

### **5.4 获取用户周边排名** (新增)

**端点**: `GET /api/v1/gamedata/leaderboard/{type}/{game_type}/surrounding/{user_id}`

**功能**: 获取用户周边的排名（用于显示用户周围的玩家）

**路径参数**:
- `type`: 排行榜类型 (rating/win_streak等)
- `game_type`: 游戏类型
- `user_id`: 用户ID

**查询参数**:
```
range=5             // 可选：返回前后各N名，默认5
scope=global        // 可选：排行榜范围
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "Surrounding ranks retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "type": "rating",
    "scope": "global",
    "user_id": "user_12345",
    "user_rank": 15,
    "total_players": 1250,
    "entries": [
      {"rank": 10, "user_id": "user_10", "display_name": "Player10", "score": 1900},
      {"rank": 11, "user_id": "user_11", "display_name": "Player11", "score": 1880},
      {"rank": 12, "user_id": "user_12", "display_name": "Player12", "score": 1870},
      {"rank": 13, "user_id": "user_13", "display_name": "Player13", "score": 1865},
      {"rank": 14, "user_id": "user_14", "display_name": "Player14", "score": 1860},
      {"rank": 15, "user_id": "user_12345", "display_name": "MyNickname", "score": 1850, "is_me": true},
      {"rank": 16, "user_id": "user_16", "display_name": "Player16", "score": 1845},
      {"rank": 17, "user_id": "user_17", "display_name": "Player17", "score": 1840},
      {"rank": 18, "user_id": "user_18", "display_name": "Player18", "score": 1835},
      {"rank": 19, "user_id": "user_19", "display_name": "Player19", "score": 1830},
      {"rank": 20, "user_id": "user_20", "display_name": "Player20", "score": 1825}
    ]
  }
}
```

---

### **5.5 刷新排行榜缓存** (新增)

**端点**: `POST /api/v1/gamedata/leaderboard/cache/refresh`

**功能**: 刷新排行榜缓存（管理员功能）

**请求JSON格式**:
```json
{
  "type": "rating"           // 可选：指定类型，为空则刷新全部
}
```

**成功响应** (200):
```json
{
  "success": true,
  "message": "Leaderboard cache refreshed",
  "service": "game_data_service",
  "timestamp": 1642145823456
}
```

---

### **5.6 获取排行榜统计** (新增)

**端点**: `GET /api/v1/gamedata/leaderboard/statistics`

**功能**: 获取排行榜系统的统计信息

**成功响应** (200):
```json
{
  "success": true,
  "message": "Leaderboard statistics retrieved successfully",
  "service": "game_data_service",
  "timestamp": 1642145823456,
  "data": {
    "enabled": true,
    "cache_enabled": true,
    "max_entries": 1000,
    "rating_count": 1250,
    "streak_count": 980,
    "playtime_count": 750,
    "experience_count": 1500,
    "achievements_count": 600
  }
}
```

---

## **🏥 6. 系统管理API**

### **6.1 健康检查**

**端点**: `GET /api/v1/gamedata/health`

**功能**: 服务健康状态检查

**成功响应** (200):
```json
{
  "status": "healthy",
  "service": "game_data_service",
  "version": "1.0.0",
  "timestamp": 1642150600,
  "uptime_seconds": 7200,
  "components": {
    "mysql": "healthy",
    "redis": "healthy",
    "http_server": "healthy",
    "timer_manager": "healthy",
    "thread_pool": "healthy"
  },
  "metrics": {
    "total_requests": 1523,
    "successful_requests": 1501,
    "failed_requests": 22,
    "cache_hit_rate": 0.85,
    "average_response_time_ms": 45
  }
}
```

**异常响应** (503):
```json
{
  "status": "unhealthy",
  "service": "game_data_service",
  "version": "1.0.0",
  "timestamp": 1642150600,
  "components": {
    "mysql": "unhealthy",
    "redis": "healthy",
    "http_server": "healthy",
    "timer_manager": "healthy",
    "thread_pool": "healthy"
  },
  "issues": [
    "MySQL连接池异常"
  ]
}
```

---

### **6.2 服务状态**

**端点**: `GET /api/v1/gamedata/stats`

**功能**: 获取详细的服务状态信息

**成功响应** (200):
```json
{
  "success": true,
  "service": "game_data_service",
  "version": "1.0.0",
  "uptime_seconds": 7200,
  "running": true,
  "timestamp": 1642150600,
  "data": {
    "mysql_pool": {
      "total_connections": 10,
      "active_connections": 2,
      "idle_connections": 8,
      "running": true
    },
    "redis_pool": {
      "total_connections": 5,
      "active_connections": 1,
      "idle_connections": 4,
      "running": true
    },
    "thread_pool": {
      "core_pool_size": 4,
      "maximum_pool_size": 16,
      "active_count": 2,
      "queue_size": 0,
      "running": true
    },
    "http_server": {
      "running": true,
      "port": 8083,
      "registered_endpoints": 16
    },
    "timer_manager": {
      "active_tasks": 5,
      "total_executed": 240,
      "success_rate": 1.0,
      "running": true
    },
    "requests": {
      "total": 1523,
      "successful": 1501,
      "failed": 22,
      "success_rate": 0.986
    }
  }
}
```

---

### **6.3 服务端点列表**

**端点**: `GET /api/v1/gamedata/service/endpoints`

**功能**: 获取服务所有可用端点(用于API网关服务发现)

**成功响应** (200):
```json
{
  "success": true,
  "service": "game_data_service",
  "version": "1.0.0",
  "endpoints": [
    {
      "method": "GET",
      "path": "/api/v1/gamedata/service/endpoints",
      "description": "服务端点发现"
    },
    {
      "method": "GET",
      "path": "/api/v1/gamedata/health",
      "description": "健康检查"
    },
    {
      "method": "GET",
      "path": "/api/v1/gamedata/stats",
      "description": "服务状态"
    },
    {
      "method": "POST",
      "path": "/api/v1/gamedata/profiles",
      "description": "创建用户档案"
    },
    {
      "method": "GET",
      "path": "/api/v1/gamedata/profiles/{user_id}",
      "description": "获取用户档案"
    },
    {
      "method": "PUT",
      "path": "/api/v1/gamedata/profiles/{user_id}",
      "description": "更新用户档案"
    },
    {
      "method": "DELETE",
      "path": "/api/v1/gamedata/profiles/{user_id}",
      "description": "删除用户档案"
    },
    {
      "method": "GET",
      "path": "/api/v1/gamedata/profiles",
      "description": "获取用户档案列表"
    },
    {
      "method": "POST",
      "path": "/api/v1/gamedata/achievements",
      "description": "解锁成就"
    },
    {
      "method": "GET",
      "path": "/api/v1/gamedata/achievements/{user_id}",
      "description": "获取用户成就"
    },
    {
      "method": "POST",
      "path": "/api/v1/gamedata/inventory",
      "description": "添加库存物品"
    },
    {
      "method": "GET",
      "path": "/api/v1/gamedata/inventory/{user_id}",
      "description": "获取用户库存"
    },
    {
      "method": "POST",
      "path": "/api/v1/gamedata/inventory/{user_id}/use",
      "description": "使用库存物品"
    },
    {
      "method": "GET",
      "path": "/api/v1/gamedata/currency/{user_id}/{currency_type}",
      "description": "获取用户货币"
    },
    {
      "method": "PUT",
      "path": "/api/v1/gamedata/currency",
      "description": "更新用户货币"
    },
    {
      "method": "GET",
      "path": "/api/v1/gamedata/currency/{user_id}",
      "description": "获取用户所有货币"
    },
    {
      "method": "POST",
      "path": "/api/v1/gamedata/leaderboard",
      "description": "更新排行榜"
    },
    {
      "method": "GET",
      "path": "/api/v1/gamedata/leaderboard/{leaderboard_type}/{game_type}",
      "description": "获取排行榜"
    },
    {
      "method": "GET",
      "path": "/api/v1/gamedata/leaderboard/{leaderboard_type}/{game_type}/rank/{user_id}",
      "description": "获取用户排名"
    }
  ],
  "total_endpoints": 19,
  "metadata": {
    "description": "Game Microservices Data Management Service",
    "architecture": "layered_data_service",
    "features": ["用户档案管理", "成就系统", "库存管理", "货币系统", "排行榜"]
  }
}
```

---

## **📝 错误响应格式**

所有HTTP API的错误响应都遵循统一格式：

```json
{
  "success": false,
  "message": "详细错误描述信息",
  "service": "game_data_service",
  "timestamp": 1642151000
}
```

**常见错误码和消息**:

### **数据验证相关**
- `Missing required field: user_id` - 缺少必需字段
- `Invalid JSON: Unexpected token` - JSON格式无效
- `Invalid user_id parameter` - 用户ID参数无效
- `Invalid pagination parameters` - 分页参数无效

### **资源相关**
- `User profile not found` - 用户档案不存在
- `Achievement not found` - 成就不存在
- `Achievement definition not found` - 成就定义不存在
- `Inventory item not found` - 库存物品不存在
- `Currency type not supported` - 不支持的货币类型
- `Leaderboard entry not found` - 排行榜条目不存在

### **业务逻辑相关**
- `Insufficient inventory quantity` - 库存数量不足
- `Currency amount cannot be negative` - 货币数量不能为负数
- `Achievement already unlocked` - 成就已解锁
- `Invalid achievement condition` - 无效的成就条件表达式
- `Invalid leaderboard type` - 无效的排行榜类型
- `Invalid leaderboard scope` - 无效的排行榜范围
- `Invalid leaderboard operation` - 无效的排行榜操作

### **系统相关**
- `Failed to create user profile` - 创建用户档案失败
- `Failed to load achievement definitions` - 加载成就定义失败
- `Database connection error` - 数据库连接错误
- `Cache operation failed` - 缓存操作失败
- `Internal error: ...` - 内部服务器错误

---

## **🎯 使用流程示例**

### **完整用户游戏数据流程**

1. **创建用户游戏档案**:
   ```http
   POST /api/v1/gamedata/profiles
   {
     "user_id": "user_12345",
     "display_name": "GamerNinja",
     "level": 1
   }
   ```

2. **用户游戏进程中更新数据**:
   ```http
   PUT /api/v1/gamedata/profiles/user_12345
   {
     "level": 5,
     "experience_points": 1200
   }
   ```

3. **解锁成就**:
   ```http
   POST /api/v1/gamedata/achievements
   {
     "user_id": "user_12345",
     "achievement_type": "first_win",
     "title": "First Victory",
     "description": "Win your first game",
     "points": 100
   }
   ```

4. **奖励物品和货币**:
   ```http
   POST /api/v1/gamedata/inventory
   {
     "user_id": "user_12345",
     "item_type": "powerup",
     "item_name": "Speed Boost",
     "quantity": 3
   }
   ```

5. **更新货币**:
   ```http
   PUT /api/v1/gamedata/currency
   {
     "user_id": "user_12345",
     "currency_type": "coins",
     "amount": 500,
     "operation": "add"
   }
   ```

6. **更新排行榜**:
   ```http
   POST /api/v1/gamedata/leaderboard
   {
     "user_id": "user_12345",
     "leaderboard_type": "global",
     "game_type": "gomoku",
     "score": 1850
   }
   ```

### **成就系统流程** (新增)

1. **获取所有成就定义**:
   ```http
   GET /api/v1/gamedata/achievements/definitions?type=milestone&rarity=rare
   ```

2. **查看用户成就进度**:
   ```http
   GET /api/v1/gamedata/achievements/user_12345/progress/win_100_games
   ```

3. **获取用户总成就点数**:
   ```http
   GET /api/v1/gamedata/achievements/user_12345/points
   ```

### **排行榜系统流程** (新增)

1. **获取评分排行榜**:
   ```http
   GET /api/v1/gamedata/leaderboard/global/gomoku?type=rating&limit=50
   ```

2. **获取用户周边排名**:
   ```http
   GET /api/v1/gamedata/leaderboard/rating/gomoku/surrounding/user_12345?range=10
   ```

3. **获取周排行榜**:
   ```http
   GET /api/v1/gamedata/leaderboard/global/gomoku?type=rating&scope=weekly
   ```

---

## **🔧 服务特性**

### **数据模型设计**
- **用户档案**: 游戏显示名称、等级、经验值、头像等
- **成就系统**: 成就类型、积分奖励、解锁时间等
- **库存系统**: 物品类型、数量、属性元数据等
- **货币系统**: 多种货币类型、数量管理、操作记录
- **排行榜**: 分类排行、分数管理、排名计算

### **数据缓存策略**
- **Redis缓存**: 用户档案、成就、库存、货币、排行榜热点数据
- **缓存TTL**: 用户档案5分钟，排行榜10分钟，库存3分钟
- **缓存键格式**: `game_data:type:identifier`
- **缓存更新**: 写操作后自动清除相关缓存

### **定时任务**
- **缓存清理任务**: 每30分钟执行，清理过期缓存数据
- **数据归档任务**: 每24小时执行，归档历史游戏数据
- **成就检查任务**: 每小时执行，检查并自动解锁符合条件的成就
- **排行榜更新任务**: 每10分钟执行，重新计算排行榜排名
- **库存清理任务**: 每6小时执行，清理过期或无效的库存物品
- **货币审计任务**: 每小时执行，审计货币变化和异常交易
- **API网关心跳**: 每30秒执行，向API网关发送心跳
- **统计信息收集**: 每15分钟执行，收集服务运行统计

### **数据一致性保证**
- **事务管理**: 复杂操作使用数据库事务确保一致性
- **乐观锁**: 更新操作使用版本号防止并发冲突
- **补偿机制**: 失败操作的回滚和补偿处理
- **数据校验**: 严格的数据格式和业务规则验证

### **性能特征**
- **并发连接**: 最大1000个并发连接
- **响应时间**: HTTP API < 100ms (95th percentile)
- **数据库连接池**: MySQL 10个连接，Redis 5个连接
- **线程池**: 核心4个线程，最大16个线程
- **缓存命中率**: 目标85%以上

---

## **🚀 实现状态总览**

| 功能模块 | 实现状态 | 特色功能 |
|---------|---------|---------|
| 用户档案管理 | ✅ 完整实现 | CRUD 完整、分页查询、缓存优化 |
| 成就系统 | ✅ 完整实现 | AchievementManager、条件表达式、多类型/稀有度、进度跟踪 |
| 库存管理 | ✅ 完整实现 | 添加、查询、使用、数量检查 |
| 货币系统 | ✅ 完整实现 | 多币种、set/add/subtract 操作、余额检查 |
| 排行榜 | ✅ 完整实现 | LeaderboardManager、多类型/范围、Redis Sorted Set、周边排名 |
| 系统监控 | ✅ 完整实现 | 健康检查、统计信息、端点发现 |
| 缓存机制 | ✅ 完整实现 | Redis缓存、TTL管理、热点数据优化 |
| 定时任务 | ✅ 完整实现 | 时间轮调度、任务监控、异常处理 |
| 游戏结束集成 | ✅ 完整实现 | 自动成就检查、排行榜更新、奖励发放 |

---

## **🔧 开发者备注**

### **数据库依赖**
- **MySQL**: 游戏数据持久化存储，支持事务和复杂查询
- **Redis**: 高频访问数据缓存，提升查询性能、排行榜存储
- **连接池**: 自动管理数据库连接，确保高效资源利用

### **架构特点**
- **分层架构**: Controller -> Service -> Repository 清晰分层
- **管理器模式**: AchievementManager 和 LeaderboardManager 封装复杂业务逻辑
- **数据模型**: 完整的游戏数据模型，支持JSON序列化
- **统一响应**: 所有API使用统一的成功/错误响应格式
- **配置管理**: 统一的配置管理系统，支持多环境部署

### **服务间集成**
- **API网关**: 自动注册服务端点，支持负载均衡
- **认证服务**: 集成JWT令牌验证，确保数据安全
- **用户服务**: 获取用户基础信息，保证数据一致性
- **游戏服务**: 提供游戏数据支撑，实时更新游戏状态

### **成就系统集成**
游戏结束时自动触发成就检查：
```
游戏结束 → GameEndProcessor.processGameEnd()
    ├── 评分计算 (EloRatingCalculator)
    ├── 奖励计算
    ├── 数据持久化
    ├── 成就检查 ← AchievementManager.checkAndUnlock()
    │   └── 成就解锁 → 发放奖励
    └── 排行榜更新 ← LeaderboardManager.updateFromProfile()
```

### **未来扩展计划**
- **实时推送**: WebSocket支持实时成就通知
- **数据分析**: 用户行为分析和游戏数据挖掘
- **A/B测试**: 支持游戏功能的A/B测试框架
- **多租户**: 支持多游戏、多租户的数据隔离

---

**📌 注意**: 本文档基于游戏数据核心服务的实际代码实现生成，所有 API 已完整实现。服务专注于游戏数据管理，包括用户档案、成就系统、库存管理、货币系统和排行榜，为整个游戏微服务架构提供可靠的数据服务基础。

**更新日期**: 2026-02-21
**实现状态**: ✅ 所有核心功能已完整实现
**版本**: v1.1.0 (新增 AchievementManager 和 LeaderboardManager)
