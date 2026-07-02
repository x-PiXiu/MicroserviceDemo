# Game Data Service 完善实现计划

## 文档信息

| 项目 | 说明 |
|------|------|
| 创建日期 | 2025-02-20 |
| 完成日期 | 2025-02-20 |
| 状态 | ✅ 已完成 |
| 优先级 | P0 |

---

## 1. 需求概述

完善 `game_data_service` 的业务逻辑实现，使其能够支撑玩家从登录到游戏的完整业务流程。

### 1.1 优先级分类

| 优先级 | 功能模块 | 说明 |
|--------|----------|------|
| **P0** | 用户档案更新 | 游戏结束后更新经验值、等级 |
| **P0** | 货币系统 | 登录后加载余额、购买/消费操作 |
| **P1** | 成就系统 | 游戏结束时解锁成就 |
| **P1** | 排行榜 | 游戏结束后更新排名 |
| **P2** | 库存管理 | 商城购买物品 |

---

## 2. 代码分析结果

### 2.1 当前实现状态

**关键发现**: `GameRepository`（数据访问层）已完成约 90%，但 `GameService`（HTTP 处理层）的 Handler 全部返回 501。

| 层级 | 实现状态 | 问题 |
|------|----------|------|
| 数据模型 (`game_models.h`) | ✅ 100% | 无 |
| Repository 层 (`game_repository.cpp`) | ✅ 90% | 排行榜方法未实现 |
| HTTP Handler (`game_service.cpp`) | ❌ 15% | 大部分返回 501 |

### 2.2 Repository 与 Handler 对比

```
Repository 方法                    HTTP Handler
─────────────────────────────────────────────────────
✅ updateUserProfile()         →   ❌ handleUpdateUserProfile (501)
✅ deleteUserProfile()         →   ❌ handleDeleteUserProfile (501)
✅ getUserProfiles()           →   ❌ handleGetUserProfiles (501)
✅ unlockAchievement()         →   ❌ handleUnlockAchievement (501)
✅ getUserAchievements()       →   ❌ handleGetUserAchievements (501)
✅ addInventoryItem()          →   ❌ handleAddInventoryItem (501)
✅ getUserInventory()          →   ❌ handleGetUserInventory (501)
✅ useInventoryItem()          →   ❌ handleUseInventoryItem (501)
✅ getUserCurrency()           →   ❌ handleGetUserCurrency (501)
✅ updateUserCurrency()        →   ❌ handleUpdateUserCurrency (501)
✅ getAllUserCurrencies()      →   ❌ handleGetAllUserCurrencies (501)
❌ updateLeaderboardEntry()    →   ❌ handleUpdateLeaderboard (501)
❌ getLeaderboard()            →   ❌ handleGetLeaderboard (501)
❌ getUserRank()               →   ❌ handleGetUserRank (501)
```

---

## 3. 实施阶段

### 阶段 1：P0 优先级 - 用户档案更新

**文件**: `game_service.cpp`

| 步骤 | 任务 | 详情 |
|------|------|------|
| 1.1 | 实现 `handleUpdateUserProfile` | 支持更新经验值、等级、显示名称 |
| 1.2 | 实现 `handleDeleteUserProfile` | 删除用户档案（账户注销） |
| 1.3 | 实现 `handleGetUserProfiles` | 分页获取用户列表（管理后台） |

**API 格式**:

```http
PUT /api/v1/gamedata/profiles/{user_id}
Content-Type: application/json

{
  "display_name": "Player123",
  "experience_points": 1500,
  "level": 5
}
```

### 阶段 2：P0 优先级 - 货币系统

**文件**: `game_service.cpp`

| 步骤 | 任务 | 详情 |
|------|------|------|
| 2.1 | 实现 `handleGetUserCurrency` | 获取单种货币余额 |
| 2.2 | 实现 `handleGetAllUserCurrencies` | 获取用户所有货币 |
| 2.3 | 实现 `handleUpdateUserCurrency` | 更新货币（支持 set/add/subtract 操作） |

### 阶段 3：P1 优先级 - 成就系统

**文件**: `game_service.cpp`

| 步骤 | 任务 | 详情 |
|------|------|------|
| 3.1 | 实现 `handleUnlockAchievement` | 解锁成就（带重复检查） |
| 3.2 | 实现 `handleGetUserAchievements` | 分页获取用户成就列表 |

### 阶段 4：P1 优先级 - 排行榜系统

**文件**: `game_repository.cpp` + `game_service.cpp`

| 步骤 | 任务 | 详情 |
|------|------|------|
| 4.1 | 实现 `updateLeaderboardEntry` | Repository 层方法 |
| 4.2 | 实现 `getLeaderboard` | Repository 层方法 |
| 4.3 | 实现 `getUserRank` | Repository 层方法 |
| 4.4 | 实现 `handleUpdateLeaderboard` | HTTP Handler |
| 4.5 | 实现 `handleGetLeaderboard` | HTTP Handler |
| 4.6 | 实现 `handleGetUserRank` | HTTP Handler |

**Redis Sorted Set 设计**:
```
Key: leaderboard:{type}:{game_type}
Member: user_id
Score: user_score
```

### 阶段 5：P2 优先级 - 库存管理

**文件**: `game_service.cpp`

| 步骤 | 任务 | 详情 |
|------|------|------|
| 5.1 | 实现 `handleAddInventoryItem` | 添加物品到库存 |
| 5.2 | 实现 `handleGetUserInventory` | 分页获取库存 |
| 5.3 | 实现 `handleUseInventoryItem` | 使用/消耗物品 |

### 阶段 6：文档更新

| 步骤 | 任务 | 详情 |
|------|------|------|
| 6.1 | 更新 `game_data_service_detail.md` | 更新实现状态为 100% |
| 6.2 | 更新 `player_game_flow.md` | 补充具体 API 调用示例 |
| 6.3 | 更新 `Game_Data_Service_API_Documentation.md` | 更新实现状态 |

---

## 4. 风险评估

| 风险 | 等级 | 缓解措施 |
|------|------|----------|
| 货币并发问题 | 中 | 使用 Redis INCRBY 原子操作 |
| 排行榜性能 | 中 | 使用 Redis Sorted Set，定期快照到 MySQL |
| 缓存一致性 | 低 | Repository 已实现 Cache-Aside 模式 |
| 缺少事务 | 中 | 货币操作添加余额检查，后期可加事务 |

---

## 5. 预估工作量

| 阶段 | 工作量 | 说明 |
|------|--------|------|
| 阶段 1 (档案更新) | 低 | Repository 已实现，只需连接 Handler |
| 阶段 2 (货币系统) | 低 | Repository 已实现，只需连接 Handler |
| 阶段 3 (成就系统) | 低 | Repository 已实现，只需连接 Handler |
| 阶段 4 (排行榜) | 中 | Repository 需实现 + Handler |
| 阶段 5 (库存管理) | 低 | Repository 已实现，只需连接 Handler |
| 阶段 6 (文档) | 低 | 文档更新 |
| **总计** | **中等** | **约 6-8 小时** |

---

## 6. 执行状态

| 阶段 | 状态 | 完成时间 |
|------|------|----------|
| 阶段 1 | ✅ 完成 | 2025-02-20 |
| 阶段 2 | ✅ 完成 | 2025-02-20 |
| 阶段 3 | ✅ 完成 | 2025-02-20 |
| 阶段 4 | ✅ 完成 | 2025-02-20 |
| 阶段 5 | ✅ 完成 | 2025-02-20 |
| 阶段 6 | ✅ 完成 | 2025-02-20 |

---

## 7. 修改的文件

| 文件 | 修改内容 |
|------|----------|
| `game_service.cpp` | 实现所有 HTTP Handler |
| `game_repository.cpp` | 实现排行榜 Repository 方法 |
| `game_data_service_detail.md` | 更新实现状态为 100% |
| `player_game_flow.md` | 补充详细 API 调用示例 |
| `Game_Data_Service_API_Documentation.md` | 更新实现状态 |

---

**计划版本**: 1.0.0
**创建日期**: 2025-02-20
**完成日期**: 2025-02-20
**状态**: ✅ 已完成
