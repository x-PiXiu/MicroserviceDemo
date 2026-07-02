# 游戏平台客户端优化开发计划

> **版本**: v1.2
> **创建日期**: 2024-02-21
> **更新日期**: 2026-02-21
> **状态**: ✅ 已完成
> **预估工期**: 7-10 个工作日
> **服务端版本**: Gomoku v2.0.3 / Game Data v1.1.0

---

## 一、项目背景

### 1.1 问题分析

当前游戏平台客户端存在以下核心问题：

| 问题类别 | 问题描述 | 严重程度 | 影响 |
|---------|---------|---------|------|
| **WebSocket 协议不兼容** | 前端使用 Socket.io-client，后端使用原生 WebSocket | 严重 | 无法正常连接游戏服务 |
| **认证流程不完整** | 未实现 welcome → authenticate → auth_success 流程 | 严重 | 认证失败 |
| **准备机制缺失** | 未实现玩家 ready 状态发送和同步 | 高 | 游戏无法正常开始 |
| **结算数据不完整** | 未展示成就解锁、排行榜变化、奖励汇总 | 中 | 用户体验不佳 |
| **货币系统未展示** | 后端已实现三层货币，前端未展示 | 中 | 功能缺失 |

### 1.2 目标

1. **协议兼容**: 将 Socket.io-client 替换为原生 WebSocket
2. **流程完整**: 实现完整的认证和准备状态机制
3. **体验增强**: 完善游戏结算界面，展示完整奖励信息
4. **功能完善**: 实现货币系统和成就通知

---

## 二、技术架构

### 2.1 当前架构

```
┌─────────────────────────────────────────────────────────┐
│                    前端 (Vue 3 + TS)                     │
├─────────────────────────────────────────────────────────┤
│  Socket.io-client  ──────► 后端原生 WebSocket (不兼容)   │
│         ↑                                               │
│         └── 认证失败，连接断开                           │
└─────────────────────────────────────────────────────────┘
```

### 2.2 目标架构

```
┌─────────────────────────────────────────────────────────┐
│                    前端 (Vue 3 + TS)                     │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌─────────────────┐    ┌─────────────────────────┐    │
│  │ NativeWebSocket │───►│ ws://172.26.26.199:80   │    │
│  │   (封装类)       │    │ /ws/gomoku/             │    │
│  └────────┬────────┘    └─────────────────────────┘    │
│           │                                             │
│           ▼                                             │
│  ┌─────────────────┐    ┌─────────────────────────┐    │
│  │ useGomokuSocket │───►│ GameHandler             │    │
│  │   (Composable)   │    │ (消息处理器)             │    │
│  └────────┬────────┘    └───────────┬─────────────┘    │
│           │                         │                   │
│           ▼                         ▼                   │
│  ┌─────────────────┐    ┌─────────────────────────┐    │
│  │    Pinia        │    │    UI Components        │    │
│  │    Stores       │    │ (CurrencyDisplay, etc.) │    │
│  └─────────────────┘    └─────────────────────────┘    │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### 2.3 网络拓扑

```
┌──────────────┐      ┌──────────────────────────────────┐
│   Browser    │      │         Nginx (172.26.26.199:80) │
│              │      │                                  │
│  ┌────────┐  │ HTTP │  /api/v1/*  ──► 各微服务 :808x   │
│  │  Vue   │◄─┼──────┤                                  │
│  │  App   │  │  WS  │  /ws/gomoku/ ──► Gomoku :8086   │
│  └────────┘  │      │                                  │
└──────────────┘      └──────────────────────────────────┘
```

---

## 三、文件变更清单

### 3.1 新增文件

| 文件路径 | 描述 | 代码行数(估) |
|---------|------|-------------|
| `src/websocket/nativesocket.ts` | 原生 WebSocket 封装类 | ~200 |
| `src/components/user/CurrencyDisplay.vue` | 货币展示组件 | ~150 |
| `src/components/user/AchievementUnlock.vue` | 成就解锁通知组件 | ~250 |

### 3.2 修改文件

| 文件路径 | 变更类型 | 描述 |
|---------|---------|------|
| `src/websocket/index.ts` | 重构 | 移除 Socket.io，使用 NativeWebSocket |
| `src/websocket/handlers/game.handler.ts` | 完善 | 添加所有消息类型处理 |
| `src/types/websocket.types.ts` | 增强 | 添加缺失的消息类型定义 |
| `src/stores/gamedata.store.ts` | 增强 | 添加货币实时更新方法 |
| `src/views/GameView.vue` | 重构 | 添加准备机制、增强结算界面 |
| `src/components/common/AppHeader.vue` | 增强 | 添加货币展示 |
| `src/utils/logger.ts` | 增强 | 添加 ws/game 命名空间 |
| `package.json` | 修改 | 移除 socket.io-client 依赖 |

---

## 四、分阶段实施计划

### 阶段 1: WebSocket 基础设施 (关键路径) ✅

**优先级**: P0
**预估工时**: 1.5 天
**依赖**: 无

#### 1.1 任务清单

- [x] 创建 `NativeWebSocket` 类 (`src/websocket/nativesocket.ts`)
  - [x] 连接管理 (connect, disconnect)
  - [x] 消息队列 (发送缓冲)
  - [x] 心跳机制 (30秒间隔)
  - [x] 自动重连 (最多5次，间隔3秒)
  - [x] 状态管理 (connecting/connected/authenticated/disconnected/error)

- [x] 重构 `useGomokuSocket` (`src/websocket/index.ts`)
  - [x] 移除 Socket.io-client 依赖
  - [x] 使用 NativeWebSocket 类
  - [x] 实现 `authenticate()` 方法
  - [x] 实现 `setReady()` 方法
  - [x] 保留现有 API 兼容性

- [x] 更新类型定义 (`src/types/websocket.types.ts`)
  - [x] 添加 `AuthenticateMessage` 类型
  - [x] 添加 `ReadyMessage` 类型
  - [x] 添加 `WelcomeMessage` 类型
  - [x] 添加 `AuthSuccessMessage` 类型

#### 1.2 核心代码结构

```typescript
// src/websocket/nativesocket.ts
export class NativeWebSocket {
  // 状态
  public status: Ref<WebSocketStatus>
  public playerId: Ref<string | null>

  // 方法
  connect(): Promise<void>
  disconnect(): void
  send(message: ClientMessage): void

  // 内部方法
  private handleMessage(message: ServerMessage): void
  private startHeartbeat(): void
  private attemptReconnect(): void
  private flushMessageQueue(): void
}
```

```typescript
// src/websocket/index.ts - useGomokuSocket
export function useGomokuSocket() {
  return {
    // 状态
    status, isConnected, isAuthenticated, playerId,

    // 连接管理
    connect, disconnect,

    // 认证与准备
    authenticate,
    setReady,

    // 游戏操作
    joinRoom, leaveRoom, makeMove,
    resign, requestDraw, requestUndo, sendChat,

    // 事件
    on, off, send,
  }
}
```

#### 1.3 消息流程

```
┌──────────┐                    ┌──────────┐
│  Client  │                    │  Server  │
└────┬─────┘                    └────┬─────┘
     │                               │
     │  connect()                    │
     │──────────────────────────────►│
     │                               │
     │         welcome               │
     │◄──────────────────────────────│
     │  { player_id: "xxx" }         │
     │                               │
     │  authenticate                 │
     │──────────────────────────────►│
     │  { token, playerId }          │
     │                               │
     │       auth_success            │
     │◄──────────────────────────────│
     │                               │
     │  ready                        │
     │──────────────────────────────►│
     │  { ready: true }              │
     │                               │
     │      ready_confirm            │
     │◄──────────────────────────────│
     │                               │
```

#### 1.4 验收标准

- [x] 原生 WebSocket 成功连接到 `ws://172.26.26.199:80/ws/gomoku/`
- [x] 收到 `welcome` 消息后能正确存储临时 ID
- [x] 发送 `authenticate` 后收到 `auth_success`
- [x] 断线后能自动重连
- [x] TypeScript 类型检查通过

---

### 阶段 2: 消息处理器完善 ✅

**优先级**: P0
**预估工时**: 1 天
**依赖**: 阶段 1

#### 2.1 任务清单

- [x] 完善 `game.handler.ts`
  - [x] `handleWelcome` - 处理服务器欢迎消息
  - [x] `handleAuthSuccess` - 处理认证成功
  - [x] `handlePlayerReady` - 处理玩家准备状态变化
  - [x] `handleAllPlayersReady` - 处理所有玩家准备状态
  - [x] `handleGameEnd` - 处理游戏结束（含成就、排行榜变化、奖励）
  - [x] `handleAchievementUnlocked` - 处理实时成就解锁通知
  - [x] `handleLeaderboardUpdate` - 处理实时排行榜更新
  - [x] `handleCurrencyUpdate` - 处理货币更新

- [x] 更新类型定义
  - [x] `GameEndMessage` - 完整游戏结束消息（含 achievements/leaderboard_changes/rewards）
  - [x] `PlayerReadyMessage` - 玩家准备状态
  - [x] `AllPlayersReadyMessage` - 全体准备状态
  - [x] `AchievementUnlockedMessage` - 成就解锁实时通知
  - [x] `LeaderboardUpdateMessage` - 排行榜更新通知
  - [x] `CurrencyUpdateMessage` - 货币更新通知

#### 2.2 消息类型映射

| 服务器消息类型 | 处理器方法 | 更新的 Store |
|---------------|-----------|-------------|
| `welcome` | `handleWelcome` | - |
| `auth_success` | `handleAuthSuccess` | authStore |
| `ready_confirm` | `handleReadyConfirm` | gameStore |
| `player_ready` | `handlePlayerReady` | gameStore |
| `all_players_ready` | `handleAllPlayersReady` | gameStore |
| `game_state` | `handleGameState` | gameStore |
| `move_result` | `handleMoveResult` | gameStore |
| `game_end` | `handleGameEnd` | gameStore, gameDataStore |
| `achievement_unlocked` | `handleAchievementUnlocked` | gameDataStore |
| `leaderboard_update` | `handleLeaderboardUpdate` | gameDataStore |
| `currency_update` | `handleCurrencyUpdate` | gameDataStore |
| `time_update` | `handleTimeUpdate` | gameStore |
| `chat` | `handleChat` | gameStore |
| `spectator_change` | `handleSpectatorChange` | gameStore |
| `notification` | `handleNotification` | gameStore |

#### 2.3 验收标准

- [x] 所有消息类型都有对应的处理器
- [x] 处理器正确更新 Pinia Store
- [x] 错误消息能正确显示给用户
- [x] TypeScript 类型完整且正确

---

### 阶段 3: 货币展示组件 (可并行) ✅

**优先级**: P1
**预估工时**: 0.5 天
**依赖**: 无

#### 3.1 任务清单

- [x] 创建 `CurrencyDisplay.vue` 组件
  - [x] 支持三种货币展示 (金币/宝石/荣誉)
  - [x] 紧凑模式和完整模式
  - [x] 数字格式化 (1K, 1M)
  - [x] 与 gameDataStore 集成

- [x] 增强 `GameDataStore`
  - [x] 添加 `honor` getter
  - [x] 添加 `updateCurrencyRealtime()` 方法
  - [x] 添加 `updateCurrenciesBatch()` 方法

#### 3.2 组件 API

```vue
<!-- 完整模式 -->
<CurrencyDisplay show-all show-label />

<!-- 紧凑模式 (头部用) -->
<CurrencyDisplay compact show-coins show-gems show-honor />

<!-- 自定义值 (结算用) -->
<CurrencyDisplay
  :coins="100"
  :gems="5"
  :honor="50"
  show-label
/>
```

#### 3.3 验收标准

- [x] 组件能正确显示当前用户货币
- [x] 数字格式化正确 (>= 1000 显示为 1K)
- [x] 货币更新后界面自动刷新
- [x] 响应式布局正确

---

### 阶段 4: 成就解锁通知组件 (可并行) ✅

**优先级**: P1
**预估工时**: 0.5 天
**依赖**: 无

#### 4.1 任务清单

- [x] 创建 `AchievementUnlock.vue` 组件
  - [x] 成就信息展示 (图标/标题/描述/积分)
  - [x] 稀有度样式 (common/rare/epic/legendary)
  - [x] 奖励展示
  - [x] 自动关闭 (5秒)
  - [x] 弹出动画效果

#### 4.2 组件 API

```vue
<AchievementUnlock
  v-model="showAchievement"
  :achievement="{
    achievement_id: 'first_win',
    title: '初次胜利',
    description: '赢得第一场游戏',
    points: 100,
    rarity: 'rare'
  }"
  :rewards="[
    { type: 'coins', amount: 100 },
    { type: 'gems', amount: 10 }
  ]"
  :auto-close="true"
  :auto-close-delay="5000"
  @closed="handleAchievementClosed"
/>
```

#### 4.3 稀有度样式

| 稀有度 | 背景色 | 光效 |
|-------|-------|------|
| common | 灰色渐变 | 无 |
| rare | 蓝色渐变 | 无 |
| epic | 紫色渐变 | 无 |
| legendary | 金色渐变 | 脉冲发光 |

#### 4.4 验收标准

- [x] 成就信息完整展示
- [x] 动画效果流畅
- [x] 自动关闭功能正常
- [x] 不同稀有度样式正确

---

### 阶段 5: GameView 重构 (核心) ✅

**优先级**: P0
**预估工时**: 2 天
**依赖**: 阶段 1, 2, 3, 4

#### 5.1 任务清单

- [x] 添加准备状态机制
  - [x] 新增 `myReadyState` 状态
  - [x] 新增 `opponentReadyState` 状态
  - [x] 实现 `toggleReady()` 方法
  - [x] 添加准备按钮 UI
  - [x] 监听 `player_ready` 和 `all_players_ready`

- [x] 更新 WebSocket 事件监听
  - [x] 添加 `welcome` 监听
  - [x] 添加 `auth_success` 监听
  - [x] 添加 `ready_confirm` 监听
  - [x] 添加 `player_ready` 监听
  - [x] 添加 `game_end` 监听（含完整结算信息）
  - [x] 添加 `achievement_unlocked` 监听
  - [x] 添加 `leaderboard_update` 监听

- [x] 增强游戏结算界面
  - [x] 展示奖励汇总 (经验/金币/宝石/荣誉点数)
  - [x] 展示排行榜变化（评分、排名变化、连胜状态）
  - [x] 展示本局解锁的成就（含稀有度和奖励）
  - [x] 集成成就解锁动画（按稀有度显示不同效果）
  - [x] 集成货币展示

- [x] 清理旧代码
  - [x] 移除 Socket.io 相关代码
  - [x] 移除未使用的状态和方法

#### 5.2 状态变更

```typescript
// 新增状态
const myReadyState = ref(false)
const opponentReadyState = ref(false)

// game_end 消息现在包含完整结算信息
interface GameEndData {
  result: number
  winnerPiece: number
  gameStats: { totalMoves: number; gameDuration: number }
  achievements: Array<{
    achievement_id: string
    achievement_name: string
    rarity: 'COMMON' | 'RARE' | 'EPIC' | 'LEGENDARY'
    points: number
    newly_unlocked: boolean
    reward: { gold: number; gems: number; honor_points: number; experience: number }
  }>
  leaderboard_changes: {
    rating: { before: number; after: number; change: number; rank_before: number; rank_after: number }
    win_streak: { current: number; best: number }
  }
  rewards: { experience: number; gold: number; rating_change: number }
}

const gameEndData = ref<GameEndData | null>(null)
const showAchievementUnlock = ref(false)
const unlockedAchievements = ref<Achievement[]>([])

// 修改计算属性
const canStartGame = computed(() => {
  return gameStatus.value === 'waiting' &&
    players.value.black && players.value.white &&
    myReadyState.value && opponentReadyState.value &&
    currentRoom.value?.host?.id === authStore.user?.id
})
```

#### 5.3 UI 变更

**操作按钮区域**:
```
┌─────────────────────────────────────┐
│  [准备] (可切换状态)                 │  ← 新增
│  [开始游戏] (双方准备好后显示)       │  ← 修改
│  [悔棋] [求和] [认输] (游戏中显示)   │
└─────────────────────────────────────┘
```

**结算弹窗**:
```
┌─────────────────────────────────────┐
│         🏆 胜利! / 😢 失败          │
├─────────────────────────────────────┤
│  本局奖励                           │  ← 来自 game_end.rewards
│  ⭐ +200 经验  💰 +50 金币          │
│  📊 评分 +20                        │
├─────────────────────────────────────┤
│  排名变化                           │  ← 来自 leaderboard_changes
│  评分: 1500 → 1520 (+20)           │
│  排名: #156 → #148 ↑8              │
│  连胜: 5场 (最佳: 8场)              │
├─────────────────────────────────────┤
│  🎉 成就解锁                        │  ← 来自 game_end.achievements
│  [势如破竹] ⬡ 稀有                 │
│  💰+200 💎+0 🏅+10 ⭐+30            │
├─────────────────────────────────────┤
│  统计: 47步 | 12分35秒              │
├─────────────────────────────────────┤
│  [返回大厅]  [再来一局]             │
└─────────────────────────────────────┘
```

#### 5.4 验收标准

- [x] 准备按钮能正确切换状态
- [x] 双方准备好后才能开始游戏
- [x] 结算界面展示完整奖励信息
- [x] 成就解锁通知正常弹出
- [x] 货币实时更新

---

### 阶段 6: 头部货币展示 ✅

**优先级**: P1
**预估工时**: 0.5 天
**依赖**: 阶段 3

#### 6.1 任务清单

- [x] 修改 `AppHeader.vue`
  - [x] 添加 `CurrencyDisplay` 组件
  - [x] 仅登录时显示
  - [x] 响应式布局

#### 6.2 UI 布局

```
┌─────────────────────────────────────────────────────────┐
│  [Logo]  游戏   排行榜   │ 💰 1,234  💎 56  🏅 890  │ 用户名 │
│                          └──── CurrencyDisplay ────┘     │
└─────────────────────────────────────────────────────────┘
```

#### 6.3 验收标准

- [x] 货币在头部正确显示
- [x] 登出后货币隐藏
- [x] 响应式布局正常

---

### 阶段 7: 清理和优化 ✅

**优先级**: P1
**预估工时**: 0.5 天
**依赖**: 阶段 1-6

#### 7.1 任务清单

- [x] 移除依赖
  - [x] 从 package.json 移除 `socket.io-client`
  - [x] 运行 `npm install` 更新 lock 文件

- [x] 完善日志系统
  - [x] 确保 `logger.ts` 有 ws 和 game 命名空间
  - [x] 添加适当的日志级别

- [x] 代码审查
  - [x] 检查 TypeScript 类型完整性
  - [x] 检查未使用的导入
  - [x] 检查代码风格一致性

- [x] 文档更新
  - [x] 更新 README.md
  - [x] 更新相关技术文档

#### 7.2 验收标准

- [x] `npm run build` 成功
- [x] TypeScript 类型检查通过
- [x] 无 ESLint 错误
- [x] 文档已更新

---

## 五、测试计划

### 5.1 单元测试

| 模块 | 测试内容 | 优先级 |
|-----|---------|-------|
| `NativeWebSocket` | 连接/断开/重连/心跳 | P0 |
| `useGomokuSocket` | 消息发送/事件分发 | P0 |
| `CurrencyDisplay` | 格式化/展示逻辑 | P1 |
| `GameDataStore` | 货币更新方法 | P1 |

### 5.2 集成测试

| 场景 | 测试内容 | 优先级 |
|-----|---------|-------|
| 认证流程 | welcome → authenticate → auth_success | P0 |
| 准备流程 | ready → ready_confirm → all_players_ready | P0 |
| 游戏结算 | game_end (含 achievements/leaderboard_changes/rewards) | P0 |
| 成就通知 | achievement_unlocked 实时推送 | P1 |
| 排行榜更新 | leaderboard_update 实时推送 | P1 |

### 5.3 E2E 测试 (Playwright)

| 场景 | 步骤 | 优先级 |
|-----|------|-------|
| 完整游戏流程 | 登录 → 大厅 → 房间 → 准备 → 对弈 → 结算 | P0 |
| 货币更新验证 | 游戏结束 → 检查货币增加 | P1 |
| 成就解锁 | 触发成就 → 验证通知显示 | P2 |

### 5.4 测试覆盖率要求

- 单元测试: ≥ 80%
- 集成测试: 覆盖核心流程
- E2E 测试: 覆盖用户主路径

---

## 六、风险管理

### 6.1 技术风险

| 风险 | 概率 | 影响 | 缓解措施 |
|-----|-----|------|---------|
| WebSocket 连接不稳定 | 中 | 高 | 实现自动重连和消息队列 |
| 消息类型不匹配 | 中 | 中 | 完善类型定义，添加运行时验证 |
| 货币同步延迟 | 低 | 中 | 乐观更新 + 服务器确认 |
| 浏览器兼容性 | 低 | 低 | 检测 WebSocket 支持 |

### 6.2 进度风险

| 风险 | 概率 | 影响 | 缓解措施 |
|-----|-----|------|---------|
| 阶段 1 耗时超预期 | 中 | 高 | 预留缓冲时间 |
| 后端接口变更 | 低 | 高 | 与后端确认接口文档 |
| 测试不充分 | 中 | 中 | 并行开发测试用例 |

### 6.3 回滚计划

如果新版本出现严重问题：
1. 恢复 `socket.io-client` 依赖
2. 回滚 `src/websocket/index.ts` 到旧版本
3. 回滚 `GameView.vue` 相关变更

---

## 七、发布计划

### 7.1 发布检查清单

- [ ] 所有阶段验收标准通过
- [ ] 单元测试覆盖率 ≥ 80%
- [ ] E2E 测试通过
- [ ] TypeScript 类型检查通过
- [ ] ESLint 检查通过
- [ ] 代码审查通过
- [ ] 文档更新完成

### 7.2 发布步骤

1. **预发布**
   - 合并到 develop 分支
   - 运行完整测试套件
   - 部署到测试环境

2. **灰度发布**
   - 部署到生产环境
   - 监控错误日志
   - 收集用户反馈

3. **全量发布**
   - 确认无重大问题
   - 全量推送

---

## 八、附录

### A. 消息类型完整列表

#### 客户端 → 服务器

| 类型 | 描述 | 数据 |
|-----|------|------|
| `authenticate` | 认证请求 | `{ token, playerId }` |
| `ready` | 准备状态 | `{ ready: boolean }` |
| `join_room` | 加入房间 | `{ roomId }` |
| `leave_room` | 离开房间 | `{}` |
| `place_piece` | 落子 | `{ position: { row, col } }` |
| `surrender` | 认输 | `{}` |
| `draw_offer` | 求和请求 | `{}` |
| `draw_response` | 求和响应 | `{ accept: boolean }` |
| `undo_move` | 悔棋请求 | `{}` |
| `undo_response` | 悔棋响应 | `{ accept: boolean }` |
| `chat` | 聊天消息 | `{ message }` |
| `heartbeat` | 心跳 | `{}` |

#### 服务器 → 客户端

| 类型 | 描述 | 数据 |
|-----|------|------|
| `welcome` | 服务器欢迎 | `{ player_id, server_info }` |
| `auth_success` | 认证成功 | `{ playerId, authenticated }` |
| `auth_failed` | 认证失败 | `{ error_code, error_message }` |
| `ready_confirm` | 准备确认 | `{ playerId, ready }` |
| `player_ready` | 玩家准备状态 | `{ playerId, ready }` |
| `all_players_ready` | 全体准备状态 | `{ players, canStart }` |
| `game_state` | 游戏状态 | `{ board, currentPlayer, playerPieces, ... }` |
| `move_result` | 落子结果 | `{ playerId, position, success }` |
| `turn_changed` | 轮次变化 | `{ current_player }` |
| `game_end` | 游戏结束 | `{ result, winner, gameStats, achievements, leaderboard_changes, rewards }` |
| `time_update` | 时间更新 | `{ blackTime, whiteTime }` |
| `achievement_unlocked` | 成就解锁通知 | `{ achievement_id, achievement_name, rarity, points, reward }` |
| `leaderboard_update` | 排行榜更新 | `{ type, scope, old_rank, new_rank, score }` |
| `currency_update` | 货币更新 | `{ currencyType, newAmount, change }` |
| `chat` | 聊天广播 | `{ playerId, message }` |
| `spectator_change` | 观战者变化 | `{ spectatorId, joined, spectatorCount }` |
| `notification` | 系统通知 | `{ type, message }` |
| `error` | 错误消息 | `{ error_code, error_message, player_id }` |

#### `game_end` 消息完整结构 (v2.0.3)

```typescript
interface GameEndMessage {
  type: 'game_end'
  data: {
    result: number           // 1=黑胜, 2=白胜, 3=和棋
    winnerPiece: number      // 1=黑, 2=白
    gameStats: {
      totalMoves: number
      gameDuration: number   // 秒
    }
    // 成就信息 (本局解锁的成就)
    achievements: Array<{
      achievement_id: string
      achievement_name: string
      rarity: 'COMMON' | 'RARE' | 'EPIC' | 'LEGENDARY'
      points: number
      newly_unlocked: boolean
      reward: {
        gold: number
        gems: number
        honor_points: number
        experience: number
      }
    }>
    // 排行榜变化
    leaderboard_changes: {
      rating: {
        before: number
        after: number
        change: number
        rank_before: number
        rank_after: number
      }
      win_streak: {
        current: number
        best: number
      }
    }
    // 奖励汇总
    rewards: {
      experience: number
      gold: number
      rating_change: number
    }
  }
  roomId: string
  timestamp: number
}
```

### B. 货币系统说明

| 货币 | 类型标识 | 获取方式 | 用途 |
|-----|---------|---------|------|
| 金币 | `coins` | 游戏奖励、任务 | 购买道具 |
| 宝石 | `gems` | 充值、成就 | 高级功能 |
| 荣誉 | `honor_points` | 排位赛奖励 | 排名展示 |

### C. 成就系统数据模型

#### 成就类型 (AchievementType)

| 类型 | 值 | 描述 |
|------|------|------|
| MILESTONE | milestone | 里程碑成就（一次性触发） |
| PROGRESS | progress | 进度型成就（累积进度触发） |
| HIDDEN | hidden | 隐藏成就（特殊条件触发，解锁前不显示） |
| SOCIAL | social | 社交成就 |

#### 成就稀有度 (AchievementRarity)

| 稀有度 | 值 | 颜色 | 获取难度 |
|-------|------|------|---------|
| COMMON | common | 灰色 | 简单 |
| RARE | rare | 蓝色 | 普通 |
| EPIC | epic | 紫色 | 困难 |
| LEGENDARY | legendary | 金色/橙色 | 极难 |

#### 成就条件表达式语法

| 操作符 | 描述 | 示例 |
|--------|------|------|
| `>=` | 大于等于 | `total_wins >= 10` |
| `<=` | 小于等于 | `rating <= 500` |
| `==` | 等于 | `last_game_result == 1` |
| `!=` | 不等于 | `game_mode != 0` |
| `>` | 大于 | `current_win_streak > 5` |
| `<` | 小于 | `last_game_moves < 30` |
| `AND` | 逻辑与 | `total_wins >= 10 AND rating >= 1500` |
| `OR` | 逻辑或 | `rating >= 2000 OR win_streak >= 10` |

**可用上下文字段**: `total_games`, `total_wins`, `total_losses`, `current_win_streak`, `best_win_streak`, `current_rating`, `peak_rating`, `level`, `last_game_result`, `last_game_moves`, `last_game_duration`

### D. 排行榜数据模型

#### 排行榜类型 (LeaderboardType)

| 类型 | 值 | 描述 |
|------|------|------|
| RATING | rating | 评分排行榜 |
| WIN_STREAK | win_streak | 连胜排行榜 |
| PLAYTIME | playtime | 游戏时长排行榜（小时） |
| WIN_RATE | win_rate | 胜率排行榜 |
| EXPERIENCE | experience | 经验值排行榜 |
| ACHIEVEMENTS | achievements | 成就点数排行榜 |

#### 排行榜范围 (LeaderboardScope)

| 范围 | 值 | 描述 |
|------|------|------|
| GLOBAL | global | 全球排行榜 |
| WEEKLY | weekly | 周排行榜（每周重置） |
| MONTHLY | monthly | 月排行榜（每月重置） |
| SEASONAL | seasonal | 赛季排行榜 |

### E. 新增 Game Data Service API (v1.1.0)

#### 成就系统增强 API

| 端点 | 方法 | 描述 |
|------|------|------|
| `/api/v1/gamedata/achievements/definitions` | GET | 获取所有成就定义（支持 type/rarity/game_type 筛选） |
| `/api/v1/gamedata/achievements/definitions/{id}` | GET | 获取单个成就定义 |
| `/api/v1/gamedata/achievements/{user_id}/progress/{id}` | GET | 获取用户对特定成就的进度 |
| `/api/v1/gamedata/achievements/{user_id}/points` | GET | 获取用户总成就点数（含各稀有度统计） |
| `/api/v1/gamedata/achievements/statistics` | GET | 获取成就系统统计 |

#### 排行榜增强 API

| 端点 | 方法 | 描述 |
|------|------|------|
| `/api/v1/gamedata/leaderboard/{type}/{game}/surrounding/{user_id}` | GET | 获取用户周边排名（用于显示周围玩家） |
| `/api/v1/gamedata/leaderboard/cache/refresh` | POST | 刷新排行榜缓存（管理员功能） |
| `/api/v1/gamedata/leaderboard/statistics` | GET | 获取排行榜系统统计 |

#### 排行榜查询参数

```
type=rating           // 排行榜类型 (rating/win_streak/playtime/win_rate/experience/achievements)
scope=global          // 排行榜范围 (global/weekly/monthly/seasonal)
limit=50              // 返回条目数，默认50，最大100
offset=0              // 偏移量
include_user=user_id  // 包含特定用户，即使不在前N名
range=5               // 周边排名范围（前后各N名）
```

---

## 九、变更历史

| 版本 | 日期 | 作者 | 变更内容 |
|-----|------|------|---------|
| v1.0 | 2024-02-21 | Claude | 初始版本 |
| v1.1 | 2026-02-21 | Claude | 同步服务端 v2.0.3 更新：新增 achievement_unlocked/leaderboard_update 消息类型、完善 game_end 消息结构、新增成就/排行榜数据模型、新增 Game Data Service API |

---

## 十、审批签字

| 角色 | 姓名 | 签字 | 日期 |
|-----|------|------|------|
| 技术负责人 | | | |
| 产品负责人 | | | |
| 项目经理 | | | |
