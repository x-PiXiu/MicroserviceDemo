# 游戏平台客户端优化开发计划

> 文档版本: v1.0.0
> 创建日期: 2026-02-23
> 依据文档: `MicroserviceDemo/docs/architecture/game_platform_gameplay_design.md`

---

## 一、需求概述

### 1.1 背景分析

根据服务端设计文档与客户端代码的详细对比分析，客户端在以下方面存在实现差距：

| 优先级 | 模块 | 覆盖率 | 主要差距 |
|--------|------|--------|----------|
| P0 | 游戏结算 | 30% | 缺少 `game_settlement` 消息处理和 UI |
| P0 | 段位系统 | 0% | 类型定义和显示完全缺失 |
| P1 | 匹配系统 | 0% | 缺少消息类型和 UI |
| P1 | 游戏统计 | 40% | Profile 缺少评分、段位、胜负统计 |
| P1 | 货币命名 | 70% | tokens vs honor 不一致 |
| P2 | 断线重连 | 0% | 消息类型和逻辑缺失 |
| P2 | 排行榜推送 | 30% | 实时推送未处理 |
| P2 | 观战者功能 | 50% | spectator_joined 消息缺失 |

### 1.2 预期目标

- **P0 功能**: 100% 实现，核心竞技体验完整
- **P1 功能**: 100% 实现，用户体验显著提升
- **P2 功能**: 80% 实现，扩展功能可用

---

## 二、P0 - 立即修复

### 2.1 功能一：游戏结算消息处理与 UI

#### 2.1.1 文档依据

| 章节 | 内容 |
|------|------|
| §7.2 | 游戏结束结算调用链 |
| §7.3 | GameEndEvent 数据结构 |
| §16.2.4 消息 8 | `game_settlement` 消息格式 |
| §18.5 | 消息协议验证表 |

#### 2.1.2 服务端消息格式

```typescript
// 文档 §16.2.4 定义的消息格式
interface GameSettlementMessage {
  type: 'game_settlement'
  data: {
    user_id: string
    result: 'win' | 'lose' | 'draw'
    rating_before: number
    rating_after: number
    rating_change: number
    tier_before: number          // 段位枚举值
    tier_after: number
    tier_name_before: string     // 如 "黄金 II"
    tier_name_after: string
    rewards: {
      gold: number
      experience: number
      honor: number
    }
    achievements_unlocked: string[]  // 成就ID列表
    new_win_streak: number
    leaderboard_rank_change?: {
      old_rank: number
      new_rank: number
    }
  }
}
```

#### 2.1.3 实现步骤

**步骤 1: 类型定义** (`src/types/websocket.types.ts`)

```typescript
// 新增 GameSettlementMessage 类型
export interface GameSettlementMessage {
  type: 'game_settlement'
  data: {
    user_id: string
    result: 'win' | 'lose' | 'draw'
    rating_before: number
    rating_after: number
    rating_change: number
    tier_before: number
    tier_after: number
    tier_name_before: string
    tier_name_after: string
    rewards: {
      gold: number
      experience: number
      honor: number
    }
    achievements_unlocked: Array<{
      id: string
      name: string
      rarity: 'COMMON' | 'RARE' | 'EPIC' | 'LEGENDARY'
    }>
    new_win_streak: number
    leaderboard_rank_change?: {
      old_rank: number
      new_rank: number
    }
  }
  timestamp: number
}

// 添加到 ServerMessage 联合类型
export type ServerMessage =
  | ...
  | GameSettlementMessage
```

**步骤 2: Store 更新** (`src/stores/game.store.ts`)

```typescript
// 新增结算数据状态
interface GameState {
  // ... 现有字段

  // 结算数据
  settlement: {
    rating_before: number
    rating_after: number
    rating_change: number
    tier_before: number
    tier_after: number
    tier_name_before: string
    tier_name_after: string
    rewards: { gold: number; experience: number; honor: number }
    achievements_unlocked: Array<{ id: string; name: string; rarity: string }>
    new_win_streak: number
    leaderboard_rank_change?: { old_rank: number; new_rank: number }
  } | null
}

// 新增 actions
actions: {
  setSettlement(data: GameSettlementMessage['data']) {
    this.settlement = {
      rating_before: data.rating_before,
      rating_after: data.rating_after,
      rating_change: data.rating_change,
      tier_before: data.tier_before,
      tier_after: data.tier_after,
      tier_name_before: data.tier_name_before,
      tier_name_after: data.tier_name_after,
      rewards: data.rewards,
      achievements_unlocked: data.achievements_unlocked,
      new_win_streak: data.new_win_streak,
      leaderboard_rank_change: data.leaderboard_rank_change
    }
  },

  clearSettlement() {
    this.settlement = null
  }
}
```

**步骤 3: 消息处理器** (`src/websocket/handlers/game.handler.ts`)

```typescript
// 新增结算消息处理
handleGameSettlement: (
  message: GameSettlementMessage,
  callback?: MessageHandlerCallback<GameSettlementMessage>
) => {
  const gameStore = useGameStore()
  const gameDataStore = useGameDataStore()

  console.log('[GameHandler] Game settlement received:', message.data)

  // 1. 保存结算数据到 store
  gameStore.setSettlement(message.data)

  // 2. 更新货币（如果有奖励）
  if (message.data.rewards) {
    gameDataStore.updateCurrencyAfterReward(message.data.rewards)
  }

  // 3. 更新评分和段位
  gameDataStore.updateRatingAfterSettlement({
    rating: message.data.rating_after,
    tier: message.data.tier_after,
    tierName: message.data.tier_name_after
  })

  callback?.(message)
}
```

**步骤 4: WebSocket 监听** (`src/views/GameView.vue`)

```typescript
// setupWebSocketListeners 函数中添加
wsOn('game_settlement', (message: GameSettlementMessage) => {
  console.log('[GameView] game_settlement received:', message)

  gameHandlers.handleGameSettlement(message)

  // 如果有新解锁的成就，显示通知
  if (message.data.achievements_unlocked?.length > 0) {
    message.data.achievements_unlocked.forEach(achievement => {
      ElMessage.success(`解锁成就: ${achievement.name}`)
    })
  }
})
```

**步骤 5: 结算 UI 组件** (`src/components/game/GameSettlementModal.vue`)

```vue
<template>
  <el-dialog
    v-model="visible"
    title="游戏结算"
    width="500px"
    center
    :close-on-click-modal="false"
    class="settlement-dialog"
  >
    <!-- 结果展示 -->
    <div class="result-header" :class="resultClass">
      <div class="result-icon">
        <el-icon v-if="isWin"><Trophy /></el-icon>
        <el-icon v-else-if="isLose"><CloseBold /></el-icon>
        <el-icon v-else><Minus /></el-icon>
      </div>
      <div class="result-text">{{ resultText }}</div>
    </div>

    <!-- 评分变化 -->
    <div class="rating-section">
      <div class="rating-change" :class="ratingChangeClass">
        <span class="rating-label">评分变化</span>
        <span class="rating-value">
          {{ settlement?.rating_before }} → {{ settlement?.rating_after }}
          <span class="change-badge">{{ ratingChangeText }}</span>
        </span>
      </div>

      <!-- 段位变化 -->
      <div class="tier-change" v-if="tierChanged">
        <div class="tier-old">
          <TierBadge :tier="settlement?.tier_before" :name="settlement?.tier_name_before" />
        </div>
        <el-icon class="tier-arrow"><ArrowRight /></el-icon>
        <div class="tier-new">
          <TierBadge :tier="settlement?.tier_after" :name="settlement?.tier_name_after" />
        </div>
      </div>
    </div>

    <!-- 奖励展示 -->
    <div class="rewards-section" v-if="hasRewards">
      <h4>获得奖励</h4>
      <div class="rewards-grid">
        <div class="reward-item" v-if="settlement?.rewards?.gold">
          <el-icon class="gold"><Coin /></el-icon>
          <span class="value">+{{ settlement.rewards.gold }}</span>
          <span class="label">金币</span>
        </div>
        <div class="reward-item" v-if="settlement?.rewards?.experience">
          <el-icon class="exp"><StarFilled /></el-icon>
          <span class="value">+{{ settlement.rewards.experience }}</span>
          <span class="label">经验</span>
        </div>
        <div class="reward-item" v-if="settlement?.rewards?.honor">
          <el-icon class="honor"><Medal /></el-icon>
          <span class="value">+{{ settlement.rewards.honor }}</span>
          <span class="label">荣誉</span>
        </div>
      </div>
    </div>

    <!-- 成就解锁 -->
    <div class="achievements-section" v-if="hasAchievements">
      <h4>成就解锁</h4>
      <div class="achievements-list">
        <div
          v-for="achievement in settlement?.achievements_unlocked"
          :key="achievement.id"
          class="achievement-item"
          :class="achievement.rarity.toLowerCase()"
        >
          <el-icon><Trophy /></el-icon>
          <span class="name">{{ achievement.name }}</span>
          <el-tag :type="getAchievementTagType(achievement.rarity)" size="small">
            {{ getRarityText(achievement.rarity) }}
          </el-tag>
        </div>
      </div>
    </div>

    <!-- 连胜展示 -->
    <div class="streak-section" v-if="settlement?.new_win_streak > 1">
      <el-icon class="fire"><FireFilled /></el-icon>
      <span>{{ settlement.new_win_streak }} 连胜！</span>
    </div>

    <template #footer>
      <el-button @click="handleClose">关闭</el-button>
      <el-button type="primary" @click="handlePlayAgain">再来一局</el-button>
    </template>
  </el-dialog>
</template>
```

#### 2.1.4 依赖文件

| 文件路径 | 修改类型 |
|----------|----------|
| `src/types/websocket.types.ts` | 新增类型 |
| `src/stores/game.store.ts` | 新增状态和 actions |
| `src/websocket/handlers/game.handler.ts` | 新增 handler |
| `src/views/GameView.vue` | 新增消息监听 |
| `src/components/game/GameSettlementModal.vue` | 新建组件 |
| `src/components/game/TierBadge.vue` | 新建组件（依赖 2.2） |

---

### 2.2 功能二：段位系统字段与显示

#### 2.2.1 文档依据

| 章节 | 内容 |
|------|------|
| §4.2.1 | ELO 评分机制 |
| §4.2.2 | 段位划分表（16 个段位） |
| §8.1.1 | user_profile 表结构（current_rating, peak_rating） |
| §9.1.1 | GET /profiles/{user_id} 返回格式 |

#### 2.2.2 段位定义（文档 §4.2.2）

```
段位         评分范围        图标
───────────────────────────────────
青铜 III     100-399         🥉
青铜 II      400-599         🥉
青铜 I       600-799         🥉
白银 III     800-999         🥈
白银 II      1000-1199       🥈
白银 I       1200-1399       🥈
黄金 III     1400-1599       🥇
黄金 II      1600-1799       🥇
黄金 I       1800-1999       🥇
铂金 III     2000-2199       💎
铂金 II      2200-2399       💎
铂金 I       2400-2599       💎
钻石 III     2600-2799       💠
钻石 II      2800-2899       💠
钻石 I       2900-2999       💠
大师         3000            👑
```

#### 2.2.3 实现步骤

**步骤 1: 段位类型定义** (`src/types/tier.types.ts` - 新建)

```typescript
/**
 * 段位系统类型定义
 * 文档依据: §4.2.2 段位划分
 */

// 段位枚举
export enum Tier {
  BRONZE_III = 0,
  BRONZE_II = 1,
  BRONZE_I = 2,
  SILVER_III = 3,
  SILVER_II = 4,
  SILVER_I = 5,
  GOLD_III = 6,
  GOLD_II = 7,
  GOLD_I = 8,
  PLATINUM_III = 9,
  PLATINUM_II = 10,
  PLATINUM_I = 11,
  DIAMOND_III = 12,
  DIAMOND_II = 13,
  DIAMOND_I = 14,
  MASTER = 15
}

// 段位配置
export const TIER_CONFIG: Record<Tier, {
  name: string
  nameEn: string
  icon: string
  color: string
  minRating: number
  maxRating: number
}> = {
  [Tier.BRONZE_III]: { name: '青铜 III', nameEn: 'Bronze III', icon: '🥉', color: '#CD7F32', minRating: 100, maxRating: 399 },
  [Tier.BRONZE_II]: { name: '青铜 II', nameEn: 'Bronze II', icon: '🥉', color: '#CD7F32', minRating: 400, maxRating: 599 },
  [Tier.BRONZE_I]: { name: '青铜 I', nameEn: 'Bronze I', icon: '🥉', color: '#CD7F32', minRating: 600, maxRating: 799 },
  [Tier.SILVER_III]: { name: '白银 III', nameEn: 'Silver III', icon: '🥈', color: '#C0C0C0', minRating: 800, maxRating: 999 },
  [Tier.SILVER_II]: { name: '白银 II', nameEn: 'Silver II', icon: '🥈', color: '#C0C0C0', minRating: 1000, maxRating: 1199 },
  [Tier.SILVER_I]: { name: '白银 I', nameEn: 'Silver I', icon: '🥈', color: '#C0C0C0', minRating: 1200, maxRating: 1399 },
  [Tier.GOLD_III]: { name: '黄金 III', nameEn: 'Gold III', icon: '🥇', color: '#FFD700', minRating: 1400, maxRating: 1599 },
  [Tier.GOLD_II]: { name: '黄金 II', nameEn: 'Gold II', icon: '🥇', color: '#FFD700', minRating: 1600, maxRating: 1799 },
  [Tier.GOLD_I]: { name: '黄金 I', nameEn: 'Gold I', icon: '🥇', color: '#FFD700', minRating: 1800, maxRating: 1999 },
  [Tier.PLATINUM_III]: { name: '铂金 III', nameEn: 'Platinum III', icon: '💎', color: '#E5E4E2', minRating: 2000, maxRating: 2199 },
  [Tier.PLATINUM_II]: { name: '铂金 II', nameEn: 'Platinum II', icon: '💎', color: '#E5E4E2', minRating: 2200, maxRating: 2399 },
  [Tier.PLATINUM_I]: { name: '铂金 I', nameEn: 'Platinum I', icon: '💎', color: '#E5E4E2', minRating: 2400, maxRating: 2599 },
  [Tier.DIAMOND_III]: { name: '钻石 III', nameEn: 'Diamond III', icon: '💠', color: '#B9F2FF', minRating: 2600, maxRating: 2799 },
  [Tier.DIAMOND_II]: { name: '钻石 II', nameEn: 'Diamond II', icon: '💠', color: '#B9F2FF', minRating: 2800, maxRating: 2899 },
  [Tier.DIAMOND_I]: { name: '钻石 I', nameEn: 'Diamond I', icon: '💠', color: '#B9F2FF', minRating: 2900, maxRating: 2999 },
  [Tier.MASTER]: { name: '大师', nameEn: 'Master', icon: '👑', color: '#FF6B6B', minRating: 3000, maxRating: 9999 }
}

// 根据评分获取段位
export function getTierByRating(rating: number): Tier {
  if (rating >= 3000) return Tier.MASTER
  if (rating >= 2900) return Tier.DIAMOND_I
  if (rating >= 2800) return Tier.DIAMOND_II
  if (rating >= 2600) return Tier.DIAMOND_III
  if (rating >= 2400) return Tier.PLATINUM_I
  if (rating >= 2200) return Tier.PLATINUM_II
  if (rating >= 2000) return Tier.PLATINUM_III
  if (rating >= 1800) return Tier.GOLD_I
  if (rating >= 1600) return Tier.GOLD_II
  if (rating >= 1400) return Tier.GOLD_III
  if (rating >= 1200) return Tier.SILVER_I
  if (rating >= 1000) return Tier.SILVER_II
  if (rating >= 800) return Tier.SILVER_III
  if (rating >= 600) return Tier.BRONZE_I
  if (rating >= 400) return Tier.BRONZE_II
  return Tier.BRONZE_III
}

// 获取段位信息
export function getTierInfo(tier: Tier) {
  return TIER_CONFIG[tier]
}

// 获取下一个段位（如果存在）
export function getNextTier(tier: Tier): Tier | null {
  if (tier === Tier.MASTER) return null
  return tier + 1 as Tier
}

// 计算到下一段位需要的评分
export function getRatingToNextTier(rating: number): { needed: number; current: number } {
  const currentTier = getTierByRating(rating)
  const nextTier = getNextTier(currentTier)
  if (!nextTier) return { needed: 0, current: rating }

  const nextTierConfig = TIER_CONFIG[nextTier]
  return {
    needed: nextTierConfig.minRating - rating,
    current: rating
  }
}
```

**步骤 2: 段位徽章组件** (`src/components/common/TierBadge.vue` - 新建)

```vue
<template>
  <div class="tier-badge" :class="sizeClass" :style="badgeStyle">
    <span class="tier-icon">{{ tierInfo?.icon }}</span>
    <span class="tier-name" v-if="showName">{{ tierInfo?.name || name }}</span>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { Tier, getTierInfo, getTierByRating, TIER_CONFIG } from '@/types/tier.types'

const props = defineProps<{
  tier?: Tier | number
  name?: string
  rating?: number
  size?: 'small' | 'medium' | 'large'
  showName?: boolean
}>()

const actualTier = computed(() => {
  if (props.tier !== undefined) {
    return typeof props.tier === 'number' ? props.tier as Tier : props.tier
  }
  if (props.rating !== undefined) {
    return getTierByRating(props.rating)
  }
  return Tier.BRONZE_III
})

const tierInfo = computed(() => getTierInfo(actualTier.value))

const sizeClass = computed(() => `size-${props.size || 'medium'}`)

const badgeStyle = computed(() => ({
  '--tier-color': tierInfo.value?.color
}))
</script>

<style scoped>
.tier-badge {
  display: inline-flex;
  align-items: center;
  gap: 0.25rem;
  padding: 0.25rem 0.5rem;
  background: linear-gradient(135deg, var(--tier-color) 0%, color-mix(in srgb, var(--tier-color) 70%, black) 100%);
  border-radius: 4px;
  color: white;
  font-weight: 600;
}

.size-small { font-size: 0.75rem; padding: 0.125rem 0.25rem; }
.size-medium { font-size: 0.875rem; }
.size-large { font-size: 1rem; padding: 0.5rem 0.75rem; }

.tier-icon { font-size: 1.2em; }
</style>
```

**步骤 3: 扩展 GameProfile 类型** (`src/types/gamedata.types.ts`)

```typescript
// 扩展 GameProfile 接口
export interface GameProfile {
  user_id: string
  display_name: string
  level: number
  experience_points: number
  avatar_url?: string
  created_at: number
  last_login_at: number

  // 新增：评分系统（文档 §8.1.1）
  current_rating: number
  peak_rating: number

  // 新增：段位信息
  tier: number            // 段位枚举值
  tier_name: string       // 段位名称，如 "黄金 II"

  // 新增：统计数据（文档 §8.1.1）
  total_games: number
  wins: number
  losses: number
  draws: number
  current_win_streak: number
  best_win_streak: number
  total_playtime_seconds: number
}
```

**步骤 4: 更新 ProfileView 显示段位** (`src/views/ProfileView.vue`)

```vue
<!-- 在头像旁边添加段位展示 -->
<div class="tier-section" v-if="gameProfile">
  <TierBadge
    :tier="gameProfile.tier"
    :name="gameProfile.tier_name"
    size="large"
    show-name
  />
  <div class="rating-info">
    <span class="rating-value">{{ gameProfile.current_rating }}</span>
    <span class="rating-label">评分</span>
    <span class="peak-rating">最高: {{ gameProfile.peak_rating }}</span>
  </div>
</div>

<!-- 添加游戏统计卡片 -->
<div class="stats-card">
  <h3>对战统计</h3>
  <div class="stats-grid">
    <div class="stat-item">
      <span class="stat-value">{{ gameProfile?.total_games || 0 }}</span>
      <span class="stat-label">总场次</span>
    </div>
    <div class="stat-item win">
      <span class="stat-value">{{ gameProfile?.wins || 0 }}</span>
      <span class="stat-label">胜场</span>
    </div>
    <div class="stat-item lose">
      <span class="stat-value">{{ gameProfile?.losses || 0 }}</span>
      <span class="stat-label">负场</span>
    </div>
    <div class="stat-item draw">
      <span class="stat-value">{{ gameProfile?.draws || 0 }}</span>
      <span class="stat-label">平局</span>
    </div>
  </div>
  <div class="streak-info">
    <span>当前连胜: {{ gameProfile?.current_win_streak || 0 }}</span>
    <span>最佳连胜: {{ gameProfile?.best_win_streak || 0 }}</span>
  </div>
</div>
```

#### 2.2.4 依赖文件

| 文件路径 | 修改类型 |
|----------|----------|
| `src/types/tier.types.ts` | 新建文件 |
| `src/types/gamedata.types.ts` | 扩展类型 |
| `src/components/common/TierBadge.vue` | 新建组件 |
| `src/views/ProfileView.vue` | 添加段位和统计显示 |
| `src/views/LeaderboardView.vue` | 添加段位列显示 |
| `src/views/GameView.vue` | 玩家信息添加段位显示 |

---

## 三、P1 - 尽快修复

### 3.1 功能三：自动匹配系统 UI

#### 3.1.1 文档依据

| 章节 | 内容 |
|------|------|
| §15.3.1 | 匹配系统（MatchMakingManager） |
| §16.2.4 消息 11-15 | match_start, match_cancel, match_found, match_status_update, match_timeout |

#### 3.1.2 服务端消息格式

```typescript
// 客户端发送
interface MatchStartMessage {
  type: 'match_start'
  data: {
    mode: 'ranked' | 'casual'
    gameMode: 'freestyle' | 'renju' | 'swap2'
  }
}

interface MatchCancelMessage {
  type: 'match_cancel'
}

// 服务端推送
interface MatchFoundMessage {
  type: 'match_found'
  data: {
    match_id: string
    room_id: string
    players: Array<{
      player_id: string
      rating: number
      piece: number  // 1=黑, 2=白
    }>
    game_mode: string
  }
}

interface MatchStatusUpdateMessage {
  type: 'match_status_update'
  data: {
    waitSeconds: number
    poolSize: number
  }
}

interface MatchTimeoutMessage {
  type: 'match_timeout'
  data: {
    message: string
  }
}
```

#### 3.1.3 实现步骤

**步骤 1: 类型定义** (`src/types/websocket.types.ts`)

```typescript
// 匹配系统消息类型
export interface MatchStartMessage {
  type: 'match_start'
  data: {
    mode: 'ranked' | 'casual'
    gameMode: 'freestyle' | 'renju' | 'swap2'
  }
}

export interface MatchCancelMessage {
  type: 'match_cancel'
}

export interface MatchStartResponseMessage {
  type: 'match_start_response'
  data: {
    success: boolean
    message: string
  }
}

export interface MatchFoundMessage {
  type: 'match_found'
  data: {
    match_id: string
    room_id: string
    players: Array<{
      player_id: string
      rating: number
      piece: number
    }>
    game_mode: string
  }
  timestamp: number
}

export interface MatchStatusUpdateMessage {
  type: 'match_status_update'
  data: {
    waitSeconds: number
    poolSize: number
  }
  timestamp: number
}

export interface MatchTimeoutMessage {
  type: 'match_timeout'
  data: {
    message: string
  }
  timestamp: number
}
```

**步骤 2: 匹配状态 Store** (`src/stores/match.store.ts` - 新建)

```typescript
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useMatchStore = defineStore('match', () => {
  // 状态
  const isMatching = ref(false)
  const matchMode = ref<'ranked' | 'casual'>('casual')
  const gameMode = ref<string>('freestyle')
  const waitSeconds = ref(0)
  const poolSize = ref(0)
  const error = ref<string | null>(null)

  // 计时器
  let waitTimer: ReturnType<typeof setInterval> | null = null

  // 开始匹配
  function startMatching(mode: 'ranked' | 'casual', game: string) {
    isMatching.value = true
    matchMode.value = mode
    gameMode.value = game
    waitSeconds.value = 0
    error.value = null

    // 开始计时
    waitTimer = setInterval(() => {
      waitSeconds.value++
    }, 1000)
  }

  // 取消匹配
  function cancelMatching() {
    isMatching.value = false
    if (waitTimer) {
      clearInterval(waitTimer)
      waitTimer = null
    }
  }

  // 匹配成功
  function matchFound() {
    isMatching.value = false
    if (waitTimer) {
      clearInterval(waitTimer)
      waitTimer = null
    }
  }

  // 匹配超时
  function matchTimeout(message: string) {
    isMatching.value = false
    error.value = message
    if (waitTimer) {
      clearInterval(waitTimer)
      waitTimer = null
    }
  }

  // 更新状态
  function updateStatus(wait: number, pool: number) {
    waitSeconds.value = wait
    poolSize.value = pool
  }

  return {
    isMatching,
    matchMode,
    gameMode,
    waitSeconds,
    poolSize,
    error,
    startMatching,
    cancelMatching,
    matchFound,
    matchTimeout,
    updateStatus
  }
})
```

**步骤 3: 匹配 UI 组件** (`src/components/match/MatchingPanel.vue` - 新建)

```vue
<template>
  <div class="matching-panel">
    <!-- 匹配按钮 -->
    <div v-if="!matchStore.isMatching" class="match-options">
      <h3>快速匹配</h3>
      <div class="mode-selector">
        <el-radio-group v-model="selectedMode">
          <el-radio-button value="casual">休闲模式</el-radio-button>
          <el-radio-button value="ranked">排位模式</el-radio-button>
        </el-radio-group>
      </div>
      <div class="game-mode-selector">
        <el-select v-model="selectedGameMode" placeholder="选择游戏模式">
          <el-option value="freestyle" label="自由模式" />
          <el-option value="renju" label="连珠模式" />
          <el-option value="swap2" label="Swap2规则" />
        </el-select>
      </div>
      <el-button type="primary" size="large" @click="startMatch">
        开始匹配
      </el-button>
    </div>

    <!-- 匹配中状态 -->
    <div v-else class="matching-status">
      <div class="matching-animation">
        <div class="spinner"></div>
        <p>正在寻找对手...</p>
      </div>

      <div class="match-info">
        <div class="info-item">
          <span class="label">等待时间</span>
          <span class="value">{{ formatTime(matchStore.waitSeconds) }}</span>
        </div>
        <div class="info-item">
          <span class="label">匹配池人数</span>
          <span class="value">{{ matchStore.poolSize }}</span>
        </div>
        <div class="info-item">
          <span class="label">模式</span>
          <span class="value">{{ matchStore.matchMode === 'ranked' ? '排位' : '休闲' }}</span>
        </div>
      </div>

      <el-button type="danger" @click="cancelMatch">
        取消匹配
      </el-button>
    </div>

    <!-- 匹配错误 -->
    <el-alert
      v-if="matchStore.error"
      :title="matchStore.error"
      type="error"
      show-icon
      closable
      @close="matchStore.error = null"
    />
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useMatchStore } from '@/stores/match.store'
import { useGomokuSocket } from '@/websocket'

const router = useRouter()
const matchStore = useMatchStore()
const { startMatch: wsStartMatch, cancelMatch: wsCancelMatch, on, off } = useGomokuSocket()

const selectedMode = ref<'ranked' | 'casual'>('casual')
const selectedGameMode = ref('freestyle')

function startMatch() {
  matchStore.startMatching(selectedMode.value, selectedGameMode.value)
  wsStartMatch({
    mode: selectedMode.value,
    gameMode: selectedGameMode.value
  })
}

function cancelMatch() {
  wsCancelMatch()
  matchStore.cancelMatching()
}

function formatTime(seconds: number): string {
  const mins = Math.floor(seconds / 60)
  const secs = seconds % 60
  return `${mins}:${secs.toString().padStart(2, '0')}`
}

// 监听匹配结果
on('match_found', (message) => {
  matchStore.matchFound()
  router.push(`/game/gomoku/room/${message.data.room_id}`)
})

on('match_status_update', (message) => {
  matchStore.updateStatus(message.data.waitSeconds, message.data.poolSize)
})

on('match_timeout', (message) => {
  matchStore.matchTimeout(message.data.message)
})
</script>
```

#### 3.1.4 依赖文件

| 文件路径 | 修改类型 |
|----------|----------|
| `src/types/websocket.types.ts` | 新增 5 个消息类型 |
| `src/stores/match.store.ts` | 新建文件 |
| `src/components/match/MatchingPanel.vue` | 新建组件 |
| `src/websocket/index.ts` | 新增 matchStart, matchCancel 方法 |
| `src/views/LobbyView.vue` | 集成匹配面板 |

---

### 3.2 功能四：游戏统计展示

#### 3.2.1 文档依据

| 章节 | 内容 |
|------|------|
| §8.1.1 | user_profile 表结构 |
| §9.1.1 | GET /profiles/{user_id} 返回格式 |

#### 3.2.2 实现步骤

已在 **2.2.3 步骤 4** 中详细说明，主要是在 `ProfileView.vue` 中添加：

- 总场次、胜场、负场、平局统计
- 当前连胜、最佳连胜
- 胜率计算和显示
- 游戏时长统计

---

### 3.3 功能五：统一货币类型命名

#### 3.3.1 文档依据

| 章节 | 内容 |
|------|------|
| §4.3 | 经济系统 - 三层货币体系 |
| §8.1.1 | user_profile 表 - gold_balance, gem_balance, honor_balance |
| §14.1.2 | CurrencyManager - CurrencyType 枚举 |

#### 3.3.2 当前问题

```typescript
// 客户端当前使用
type CurrencyType = 'coins' | 'gems' | 'honor_points' | 'tokens'

// 服务端文档定义
enum class CurrencyType {
    GOLD,    // 金币
    GEM,     // 宝石
    HONOR    // 荣誉点
}
```

#### 3.3.3 实现步骤

**步骤 1: 统一类型定义** (`src/types/gamedata.types.ts`)

```typescript
// 修改货币类型，与服务端保持一致
export type CurrencyType = 'gold' | 'gems' | 'honor'

// 货币显示名称映射
export const CURRENCY_NAMES: Record<CurrencyType, string> = {
  gold: '金币',
  gems: '宝石',
  honor: '荣誉点'
}

// 货币图标映射
export const CURRENCY_ICONS: Record<CurrencyType, string> = {
  gold: '🪙',
  gems: '💎',
  honor: '🏅'
}
```

**步骤 2: 更新 Store** (`src/stores/gamedata.store.ts`)

```typescript
// 修改属性名
interface GameDataState {
  gold: number      // 原 coins
  gems: number      // 不变
  honor: number     // 原 tokens
}

// 添加兼容 getter（可选，过渡期使用）
getters: {
  coins: (state) => state.gold,     // 兼容旧代码
  tokens: (state) => state.honor    // 兼容旧代码
}
```

**步骤 3: 更新 UI 组件**

搜索并替换所有使用 `tokens` 的地方改为 `honor`，使用 `coins` 的地方改为 `gold`。

---

## 四、P2 - 后续优化

### 4.1 功能六：断线重连

#### 4.1.1 文档依据

| 章节 | 内容 |
|------|------|
| §16.2.4 消息 19 | reconnect, reconnect_failed 消息 |

#### 4.1.2 实现要点

```typescript
// 消息类型
interface ReconnectMessage {
  type: 'reconnect'
  data: { roomId: string }
}

interface ReconnectFailedMessage {
  type: 'reconnect_failed'
  data: { reason: string }
}

// 重连逻辑
async function handleReconnect(roomId: string) {
  // 1. 发送重连请求
  ws.send({ type: 'reconnect', data: { roomId } })

  // 2. 收到 room_joined 表示重连成功
  // 3. 收到 reconnect_failed 表示重连失败
}
```

---

### 4.2 功能七：排行榜实时推送

#### 4.2.1 文档依据

| 章节 | 内容 |
|------|------|
| §15.3.2 | 排行榜实时推送（leaderboard_push_service） |
| §18.5 | leaderboard_update 消息 |

#### 4.2.2 实现要点

```typescript
// 已定义的消息类型
interface LeaderboardUpdateMessage {
  type: 'leaderboard_update'
  data: {
    type: string
    scope: string
    old_rank: number
    new_rank: number
    score: number
  }
}

// 处理逻辑
on('leaderboard_update', (message) => {
  // 显示排名变化通知
  if (message.data.new_rank < message.data.old_rank) {
    showNotification(`排名上升至 #${message.data.new_rank}！`)
  }
})
```

---

### 4.3 功能八：完善观战者功能

#### 4.3.1 文档依据

| 章节 | 内容 |
|------|------|
| §16.2.4 消息 28-29 | spectator_joined, spectator_change |

#### 4.3.2 实现要点

```typescript
// 新增消息类型
interface SpectatorJoinedMessage {
  type: 'spectator_joined'
  data: {
    board: number[][]
    gamePhase: string
    currentTurn: number
    playerPieces: Record<string, number>
  }
}

// 观战者模式 UI
// - 只读棋盘（不能落子）
// - 显示玩家信息
// - 显示观战者列表
```

---

## 五、实施计划

### 5.1 阶段划分

```
┌─────────────────────────────────────────────────────────────────┐
│                      实施阶段划分                                │
└─────────────────────────────────────────────────────────────────┘

阶段 0: 基础设施准备（1天）
├── 创建 tier.types.ts 段位类型
├── 创建 TierBadge.vue 组件
└── 扩展 gamedata.types.ts 类型定义

阶段 1: P0 核心功能（3天）
├── 1.1 游戏结算消息处理（1天）
│   ├── websocket.types.ts 添加 GameSettlementMessage
│   ├── game.handler.ts 添加 handleGameSettlement
│   └── GameView.vue 添加消息监听
│
├── 1.2 结算 UI 组件（1天）
│   ├── GameSettlementModal.vue
│   └── 集成到 GameView.vue
│
└── 1.3 段位系统显示（1天）
    ├── ProfileView.vue 添加段位和统计
    └── LeaderboardView.vue 添加段位列

阶段 2: P1 重要功能（3天）
├── 2.1 匹配系统（2天）
│   ├── websocket.types.ts 添加匹配消息
│   ├── match.store.ts
│   ├── MatchingPanel.vue
│   └── LobbyView.vue 集成
│
└── 2.2 货币命名统一（0.5天）
    └── 全局替换和测试

阶段 3: P2 扩展功能（2天）
├── 3.1 断线重连（1天）
├── 3.2 排行榜推送（0.5天）
└── 3.3 观战者完善（0.5天）

总计：约 9 天
```

### 5.2 测试计划

| 功能 | 测试类型 | 测试内容 |
|------|----------|----------|
| 游戏结算 | E2E | 完成一场游戏，验证结算弹窗显示 |
| 段位显示 | 单元/E2E | 验证评分到段位的转换 |
| 匹配系统 | E2E | 两个客户端匹配，验证配对成功 |
| 货币更新 | 集成 | 游戏结束后验证货币变化 |

---

## 六、风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 服务端消息格式变更 | 高 | 先与后端确认消息格式，编写兼容代码 |
| 现有代码耦合 | 中 | 渐进式重构，保持向后兼容 |
| UI 设计不一致 | 低 | 参考现有组件风格，使用统一的 Element Plus 主题 |

---

## 七、附录

### 7.1 文档章节索引

| 章节 | 标题 | 主要内容 |
|------|------|----------|
| §4.2 | ELO 评分系统 | 评分机制、段位划分 |
| §4.3 | 经济系统 | 三层货币体系 |
| §5 | 排行榜系统 | 排行榜类型、实现 |
| §6 | 成就系统 | 成就分类、稀有度 |
| §7.2 | 游戏结算调用链 | 完整的结算流程 |
| §8.1.1 | user_profile 表 | 用户档案数据结构 |
| §9.1 | Game Data API | API 端点定义 |
| §15.3.1 | 匹配系统 | MatchMakingManager |
| §16.2.4 | WebSocket 消息协议 | 31 条消息详细格式 |
| §18.5 | 消息协议验证表 | 消息实现状态 |

### 7.2 关键文件列表

```
src/
├── types/
│   ├── websocket.types.ts    # WebSocket 消息类型
│   ├── gamedata.types.ts     # 游戏数据类型
│   └── tier.types.ts         # 段位类型（新建）
├── stores/
│   ├── game.store.ts         # 游戏状态
│   ├── gamedata.store.ts     # 游戏数据状态
│   └── match.store.ts        # 匹配状态（新建）
├── views/
│   ├── GameView.vue          # 游戏页面
│   ├── ProfileView.vue       # 用户资料页
│   └── LobbyView.vue         # 大厅页面
├── components/
│   ├── game/
│   │   └── GameSettlementModal.vue  # 结算弹窗（新建）
│   ├── common/
│   │   └── TierBadge.vue            # 段位徽章（新建）
│   └── match/
│       └── MatchingPanel.vue        # 匹配面板（新建）
└── websocket/
    └── handlers/
        └── game.handler.ts   # 消息处理器
```

---

**文档状态**: 待确认
**下一步**: 用户确认后开始阶段 0 实施
