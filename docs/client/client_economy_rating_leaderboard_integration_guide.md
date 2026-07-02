# 客户端集成指南：经济系统、评分系统、排行榜系统

## 文档概述

本文档详细说明客户端如何集成游戏平台的三大核心系统：
- **经济系统**：货币管理（金币、宝石、荣誉）
- **评分系统**：ELO评分计算与段位管理
- **排行榜系统**：多类型排行榜查询与展示

**服务端实现状态**：✅ 全部完工

---

## 1. 系统架构概览

### 1.1 服务端点

| 系统 | 服务 | 端口 | 基础路径 |
|------|------|------|----------|
| 经济系统 | Game Data Service | 8084 | `/api/v1/gamedata/currency` |
| 评分系统 | Game Data Service | 8084 | 自动计算（游戏结算时） |
| 排行榜系统 | Game Data Service | 8084 | `/api/v1/gamedata/leaderboard` |

### 1.2 认证方式

所有 API 请求需要携带 JWT Token：

```http
Authorization: Bearer {access_token}
```

---

## 2. 经济系统集成

### 2.1 货币类型

```typescript
enum CurrencyType {
  GOLD = "gold",     // 金币 - 基础货币
  GEM = "gem",       // 宝石 - 高级货币
  HONOR = "honor"    // 荣誉值 - 竞技货币
}
```

### 2.2 货币配置（服务端）

| 货币 | 初始值 | 上限 | 用途 |
|------|--------|------|------|
| 金币 | 1000 | 9,999,999 | 购买道具、入场费 |
| 宝石 | 0 | 99,999 | 高级道具、皮肤 |
| 荣誉值 | 0 | 999,999 | 赛季奖励、排名奖励 |

### 2.3 API 接口

#### 2.3.1 查询单种货币余额

```http
GET /api/v1/gamedata/currency/{user_id}/{currency_type}
Authorization: Bearer {access_token}
```

**路径参数**：
- `user_id`: 用户ID
- `currency_type`: 货币类型（`gold`、`gem`、`honor`）

**响应示例**：

```json
{
  "success": true,
  "data": {
    "user_id": "usr_12345",
    "currency_type": "gold",
    "amount": 5680
  },
  "message": "Currency retrieved successfully"
}
```

#### 2.3.2 查询所有货币余额

```http
GET /api/v1/gamedata/currency/{user_id}
Authorization: Bearer {access_token}
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "user_id": "usr_12345",
    "currencies": [
      { "currency_type": "gold", "amount": 5680 },
      { "currency_type": "gem", "amount": 25 },
      { "currency_type": "honor", "amount": 450 }
    ],
    "total_types": 3
  },
  "message": "All currencies retrieved successfully"
}
```

#### 2.3.3 更新货币（服务间调用）

```http
PUT /api/v1/gamedata/currency
Authorization: Bearer {access_token}
Content-Type: application/json

{
  "user_id": "usr_12345",
  "currency_type": "gold",
  "amount": 100,
  "reason": "game_reward",
  "source": "gomoku",
  "reference_id": "game_67890"
}
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "transaction_id": "tx_abc123",
    "balance_before": 5580,
    "balance_after": 5680,
    "amount": 100
  }
}
```

### 2.4 客户端实现示例

#### TypeScript/Vue3 封装

```typescript
// api/currency.ts
import { http } from '@/utils/http';

// 单种货币响应
export interface CurrencyResponse {
  user_id: string;
  currency_type: string;
  amount: number;
}

// 所有货币响应
export interface AllCurrenciesResponse {
  user_id: string;
  currencies: Array<{
    currency_type: string;
    amount: number;
  }>;
  total_types: number;
}

// 便捷的货币余额对象
export interface CurrencyBalance {
  gold: number;
  gem: number;
  honor: number;
}

export const currencyApi = {
  // 获取所有货币余额
  async getBalances(userId: string): Promise<AllCurrenciesResponse> {
    const response = await http.get(`/api/v1/gamedata/currency/${userId}`);
    return response.data.data;
  },

  // 获取单种货币余额
  async getBalance(userId: string, currencyType: string): Promise<CurrencyResponse> {
    const response = await http.get(
      `/api/v1/gamedata/currency/${userId}/${currencyType}`
    );
    return response.data.data;
  },

  // 便捷方法：获取格式化的货币余额
  async getBalanceMap(userId: string): Promise<CurrencyBalance> {
    const response = await this.getBalances(userId);
    const balance: CurrencyBalance = { gold: 0, gem: 0, honor: 0 };
    for (const currency of response.currencies) {
      if (currency.currency_type === 'gold') balance.gold = currency.amount;
      else if (currency.currency_type === 'gem') balance.gem = currency.amount;
      else if (currency.currency_type === 'honor') balance.honor = currency.amount;
    }
    return balance;
  }
};
```

#### Pinia Store 集成

```typescript
// stores/currency.ts
import { defineStore } from 'pinia';
import { currencyApi, CurrencyBalance } from '@/api/currency';

export const useCurrencyStore = defineStore('currency', {
  state: () => ({
    balances: null as CurrencyBalance | null,
    loading: false,
    lastFetch: null as Date | null
  }),

  getters: {
    gold: (state) => state.balances?.gold ?? 0,
    gem: (state) => state.balances?.gem ?? 0,
    honor: (state) => state.balances?.honor ?? 0,

    // 格式化显示
    formattedGold: (state) => formatNumber(state.balances?.gold ?? 0),
    formattedGem: (state) => formatNumber(state.balances?.gem ?? 0)
  },

  actions: {
    async fetchBalances(userId: string) {
      this.loading = true;
      try {
        this.balances = await currencyApi.getBalances(userId);
        this.lastFetch = new Date();
      } finally {
        this.loading = false;
      }
    },

    // 游戏结束后刷新余额
    async refreshAfterGame(userId: string) {
      await this.fetchBalances(userId);
    }
  }
});

function formatNumber(num: number): string {
  if (num >= 1000000) {
    return (num / 1000000).toFixed(1) + 'M';
  } else if (num >= 1000) {
    return (num / 1000).toFixed(1) + 'K';
  }
  return num.toString();
}
```

---

## 3. 评分系统集成

### 3.1 段位系统

服务端定义了 16 级段位：

| 等级 | 段位 | 评分范围 | 图标 |
|------|------|----------|------|
| 0 | 青铜 III | 100-399 | 🥉 |
| 1 | 青铜 II | 400-599 | 🥉 |
| 2 | 青铜 I | 600-799 | 🥉 |
| 3 | 白银 III | 800-999 | 🥈 |
| 4 | 白银 II | 1000-1199 | 🥈 |
| 5 | 白银 I | 1200-1399 | 🥈 |
| 6 | 黄金 III | 1400-1599 | 🥇 |
| 7 | 黄金 II | 1600-1799 | 🥇 |
| 8 | 黄金 I | 1800-1999 | 🥇 |
| 9 | 铂金 III | 2000-2199 | 💎 |
| 10 | 铂金 II | 2200-2399 | 💎 |
| 11 | 铂金 I | 2400-2599 | 💎 |
| 12 | 钻石 III | 2600-2799 | 💠 |
| 13 | 钻石 II | 2800-2899 | 💠 |
| 14 | 钻石 I | 2900-2999 | 💠 |
| 15 | 大师 | 3000+ | 👑 |

### 3.2 ELO 计算参数

```typescript
interface EloConfig {
  initial_rating: 1000;      // 初始评分
  min_rating: 100;           // 最低评分
  max_rating: 3000;          // 最高评分
  k_factor_new: 32;          // 新玩家 K 因子
  k_factor_stable: 16;       // 稳定玩家 K 因子
  provisional_games: 10;     // 新手保护场次
}
```

### 3.3 评分计算逻辑

ELO 评分在游戏结算时自动计算，客户端无需调用单独接口。

#### 评分变化公式

```
新评分 = 当前评分 + K × (实际得分 - 期望得分)

其中：
- K = K因子（新玩家32，稳定玩家16）
- 实际得分 = 1.0（胜）/ 0.5（平）/ 0.0（负）
- 期望得分 = 1 / (1 + 10^((对手评分 - 自己评分) / 400))
```

### 3.4 游戏结算响应

游戏结束时，服务端返回完整的结算信息：

```json
{
  "success": true,
  "data": {
    "game_id": "game_12345",
    "settlements": [
      {
        "user_id": "usr_12345",
        "result": "WIN",
        "rating_before": 1250,
        "rating_after": 1278,
        "rating_change": 28,
        "tier_before": "SILVER_I",
        "tier_after": "SILVER_I",
        "tier_changed": false,
        "tier_promoted": false,
        "tier_demoted": false,
        "reward": {
          "experience": 100,
          "currency_rewards": [
            { "type": "gold", "amount": 50 },
            { "type": "honor", "amount": 10 }
          ]
        },
        "new_win_streak": 3,
        "achievements_unlocked": ["first_win_streak_3"]
      }
    ]
  }
}
```

### 3.5 客户端实现示例

#### 段位工具类

```typescript
// utils/tier.ts

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

export const TierInfo: Record<Tier, { name: string; icon: string; minRating: number; color: string }> = {
  [Tier.BRONZE_III]: { name: '青铜 III', icon: '🥉', minRating: 100, color: '#CD7F32' },
  [Tier.BRONZE_II]: { name: '青铜 II', icon: '🥉', minRating: 400, color: '#CD7F32' },
  [Tier.BRONZE_I]: { name: '青铜 I', icon: '🥉', minRating: 600, color: '#CD7F32' },
  [Tier.SILVER_III]: { name: '白银 III', icon: '🥈', minRating: 800, color: '#C0C0C0' },
  [Tier.SILVER_II]: { name: '白银 II', icon: '🥈', minRating: 1000, color: '#C0C0C0' },
  [Tier.SILVER_I]: { name: '白银 I', icon: '🥈', minRating: 1200, color: '#C0C0C0' },
  [Tier.GOLD_III]: { name: '黄金 III', icon: '🥇', minRating: 1400, color: '#FFD700' },
  [Tier.GOLD_II]: { name: '黄金 II', icon: '🥇', minRating: 1600, color: '#FFD700' },
  [Tier.GOLD_I]: { name: '黄金 I', icon: '🥇', minRating: 1800, color: '#FFD700' },
  [Tier.PLATINUM_III]: { name: '铂金 III', icon: '💎', minRating: 2000, color: '#E5E4E2' },
  [Tier.PLATINUM_II]: { name: '铂金 II', icon: '💎', minRating: 2200, color: '#E5E4E2' },
  [Tier.PLATINUM_I]: { name: '铂金 I', icon: '💎', minRating: 2400, color: '#E5E4E2' },
  [Tier.DIAMOND_III]: { name: '钻石 III', icon: '💠', minRating: 2600, color: '#B9F2FF' },
  [Tier.DIAMOND_II]: { name: '钻石 II', icon: '💠', minRating: 2800, color: '#B9F2FF' },
  [Tier.DIAMOND_I]: { name: '钻石 I', icon: '💠', minRating: 2900, color: '#B9F2FF' },
  [Tier.MASTER]: { name: '大师', icon: '👑', minRating: 3000, color: '#FF4500' }
};

export function getTierFromRating(rating: number): Tier {
  if (rating >= 3000) return Tier.MASTER;
  if (rating >= 2900) return Tier.DIAMOND_I;
  if (rating >= 2800) return Tier.DIAMOND_II;
  if (rating >= 2600) return Tier.DIAMOND_III;
  if (rating >= 2400) return Tier.PLATINUM_I;
  if (rating >= 2200) return Tier.PLATINUM_II;
  if (rating >= 2000) return Tier.PLATINUM_III;
  if (rating >= 1800) return Tier.GOLD_I;
  if (rating >= 1600) return Tier.GOLD_II;
  if (rating >= 1400) return Tier.GOLD_III;
  if (rating >= 1200) return Tier.SILVER_I;
  if (rating >= 1000) return Tier.SILVER_II;
  if (rating >= 800) return Tier.SILVER_III;
  if (rating >= 600) return Tier.BRONZE_I;
  if (rating >= 400) return Tier.BRONZE_II;
  return Tier.BRONZE_III;
}

export function getTierProgress(rating: number): { current: number; next: number; progress: number } {
  const tier = getTierFromRating(rating);
  const info = TierInfo[tier];

  // 计算当前段位的进度
  const nextTier = tier < Tier.MASTER ? tier + 1 : tier;
  const nextInfo = TierInfo[nextTier];

  const rangeStart = info.minRating;
  const rangeEnd = nextInfo.minRating;
  const progress = (rating - rangeStart) / (rangeEnd - rangeStart);

  return {
    current: rating - rangeStart,
    next: rangeEnd - rating,
    progress: Math.min(1, Math.max(0, progress))
  };
}
```

#### Vue 组件示例

```vue
<!-- components/TierDisplay.vue -->
<template>
  <div class="tier-display">
    <div class="tier-icon" :style="{ color: tierColor }">
      {{ tierIcon }}
    </div>
    <div class="tier-info">
      <div class="tier-name">{{ tierName }}</div>
      <div class="rating">{{ rating }}</div>
      <div class="progress-bar">
        <div
          class="progress-fill"
          :style="{ width: `${progress * 100}%`, backgroundColor: tierColor }"
        />
      </div>
      <div class="progress-text">
        距离下一段位还需 {{ nextTierPoints }} 分
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { TierInfo, getTierFromRating, getTierProgress } from '@/utils/tier';

const props = defineProps<{
  rating: number;
}>();

const tier = computed(() => getTierFromRating(props.rating));
const tierInfo = computed(() => TierInfo[tier.value]);
const progressInfo = computed(() => getTierProgress(props.rating));

const tierIcon = computed(() => tierInfo.value.icon);
const tierName = computed(() => tierInfo.value.name);
const tierColor = computed(() => tierInfo.value.color);
const progress = computed(() => progressInfo.value.progress);
const nextTierPoints = computed(() => progressInfo.value.next);
</script>
```

---

## 4. 排行榜系统集成

### 4.1 排行榜类型

```typescript
enum LeaderboardType {
  RATING = "rating",           // 评分排行榜
  WIN_STREAK = "win_streak",   // 连胜排行榜
  PLAYTIME = "playtime",       // 游戏时长排行榜
  WIN_RATE = "win_rate",       // 胜率排行榜
  EXPERIENCE = "experience",   // 经验排行榜
  ACHIEVEMENTS = "achievements" // 成就点数排行榜
}
```

### 4.2 排行榜范围

```typescript
enum LeaderboardScope {
  GLOBAL = "global",     // 全球榜
  WEEKLY = "weekly",     // 周榜
  MONTHLY = "monthly",   // 月榜
  SEASONAL = "seasonal"  // 赛季榜
}
```

### 4.3 API 接口

#### 4.3.1 获取排行榜

```http
GET /api/v1/gamedata/leaderboard/{leaderboard_type}/{game_type}
Authorization: Bearer {access_token}

Query Parameters:
- scope: string (global|weekly|monthly|seasonal) - 默认 global
- offset: number - 偏移量，默认 0
- limit: number - 数量限制，默认 50
- user_id: string - 可选，用于获取用户自己的排名
```

**请求示例**：

```http
GET /api/v1/gamedata/leaderboard/rating/gomoku?scope=global&offset=0&limit=10&user_id=usr_12345
Authorization: Bearer eyJhbGc...
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "type": "rating",
    "scope": "global",
    "updated_at": "2025-02-21T10:30:00Z",
    "total_players": 15234,
    "entries": [
      {
        "rank": 1,
        "user_id": "usr_001",
        "display_name": "棋圣",
        "score": 2850,
        "tier_name": "钻石 II",
        "win_rate": 78.5,
        "games_played": 456,
        "avatar_url": "https://..."
      },
      {
        "rank": 2,
        "user_id": "usr_002",
        "display_name": "高手",
        "score": 2780,
        "tier_name": "钻石 III",
        "win_rate": 72.3,
        "games_played": 380,
        "avatar_url": "https://..."
      }
    ],
    "my_rank": {
      "rank": 156,
      "user_id": "usr_12345",
      "display_name": "我的昵称",
      "score": 1450,
      "tier_name": "黄金 III",
      "win_rate": 55.2,
      "games_played": 89,
      "avatar_url": "https://..."
    }
  }
}
```

#### 4.3.2 获取用户排名

```http
GET /api/v1/gamedata/leaderboard/{leaderboard_type}/{game_type}/rank/{user_id}
Authorization: Bearer {access_token}

Query Parameters:
- scope: string (global|weekly|monthly|seasonal) - 默认 global
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "user_id": "usr_12345",
    "leaderboard_type": "rating",
    "game_type": "gomoku",
    "scope": "global",
    "rank": 156,
    "on_leaderboard": true,
    "display_name": "我的昵称",
    "score": 1450,
    "tier_name": "黄金 III",
    "win_rate": 55.2,
    "games_played": 89,
    "avatar_url": "https://..."
  }
}
```

### 4.4 排行榜自动更新

服务端自动处理：
- **周榜重置**：每周一 00:00 (Asia/Shanghai)
- **月榜重置**：每月 1 日 00:00 (Asia/Shanghai)
- **赛季重置**：每 90 天

### 4.5 客户端实现示例

#### API 封装

```typescript
// api/leaderboard.ts
import { http } from '@/utils/http';

export interface LeaderboardEntry {
  rank: number;
  user_id: string;
  display_name: string;
  score: number;
  tier_name: string;
  win_rate: number;
  games_played: number;
  avatar_url: string;
}

export interface LeaderboardResult {
  type: string;
  scope: string;
  updated_at: string;
  total_players: number;
  entries: LeaderboardEntry[];
  my_rank?: LeaderboardEntry;
}

export const leaderboardApi = {
  async getLeaderboard(
    type: string,
    gameType: string,
    options: {
      scope?: string;
      offset?: number;
      limit?: number;
      userId?: string;
    } = {}
  ): Promise<LeaderboardResult> {
    const params = new URLSearchParams();
    if (options.scope) params.append('scope', options.scope);
    if (options.offset) params.append('offset', options.offset.toString());
    if (options.limit) params.append('limit', options.limit.toString());
    if (options.userId) params.append('user_id', options.userId);

    const response = await http.get(
      `/api/v1/gamedata/leaderboard/${type}/${gameType}?${params.toString()}`
    );
    return response.data.data;
  },

  async getUserRank(
    type: string,
    gameType: string,
    userId: string,
    scope: string = 'global'
  ): Promise<LeaderboardEntry> {
    const response = await http.get(
      `/api/v1/gamedata/leaderboard/${type}/${gameType}/rank/${userId}?scope=${scope}`
    );
    return response.data.data;
  }
};
```

#### Pinia Store

```typescript
// stores/leaderboard.ts
import { defineStore } from 'pinia';
import { leaderboardApi, LeaderboardResult, LeaderboardEntry } from '@/api/leaderboard';

export const useLeaderboardStore = defineStore('leaderboard', {
  state: () => ({
    currentType: 'rating' as string,
    currentScope: 'global' as string,
    leaderboard: null as LeaderboardResult | null,
    myRank: null as LeaderboardEntry | null,
    loading: false,
    error: null as string | null
  }),

  getters: {
    top10: (state) => state.leaderboard?.entries.slice(0, 10) ?? [],
    hasMyRank: (state) => state.leaderboard?.my_rank !== undefined
  },

  actions: {
    async fetchLeaderboard(
      type: string,
      gameType: string,
      scope: string = 'global',
      userId?: string
    ) {
      this.loading = true;
      this.error = null;
      this.currentType = type;
      this.currentScope = scope;

      try {
        this.leaderboard = await leaderboardApi.getLeaderboard(type, gameType, {
          scope,
          limit: 50,
          userId
        });
        this.myRank = this.leaderboard?.my_rank ?? null;
      } catch (e: any) {
        this.error = e.message;
      } finally {
        this.loading = false;
      }
    },

    async fetchMyRank(type: string, gameType: string, userId: string, scope: string = 'global') {
      try {
        this.myRank = await leaderboardApi.getUserRank(type, gameType, userId, scope);
      } catch (e) {
        console.error('Failed to fetch rank:', e);
      }
    }
  }
});
```

#### Vue 组件

```vue
<!-- components/LeaderboardView.vue -->
<template>
  <div class="leaderboard">
    <!-- 类型切换 -->
    <div class="tabs">
      <button
        v-for="type in leaderboardTypes"
        :key="type.value"
        :class="['tab', { active: currentType === type.value }]"
        @click="changeType(type.value)"
      >
        {{ type.label }}
      </button>
    </div>

    <!-- 范围切换 -->
    <div class="scope-selector">
      <button
        v-for="scope in scopes"
        :key="scope.value"
        :class="['scope-btn', { active: currentScope === scope.value }]"
        @click="changeScope(scope.value)"
      >
        {{ scope.label }}
      </button>
    </div>

    <!-- 排行榜列表 -->
    <div class="leaderboard-list" v-if="!loading">
      <div
        v-for="entry in leaderboard?.entries"
        :key="entry.user_id"
        :class="['entry', { 'is-me': entry.user_id === currentUserId }]"
      >
        <div class="rank" :class="getRankClass(entry.rank)">
          {{ entry.rank <= 3 ? ['🥇', '🥈', '🥉'][entry.rank - 1] : entry.rank }}
        </div>
        <img :src="entry.avatar_url" class="avatar" />
        <div class="info">
          <div class="name">{{ entry.display_name }}</div>
          <div class="tier">{{ entry.tier_name }}</div>
        </div>
        <div class="stats">
          <div class="score">{{ entry.score }}</div>
          <div class="win-rate">{{ entry.win_rate.toFixed(1) }}%</div>
        </div>
      </div>
    </div>

    <!-- 我的排名 -->
    <div class="my-rank" v-if="myRank">
      <div class="rank">#{{ myRank.rank }}</div>
      <div class="info">
        <div class="name">{{ myRank.display_name }}</div>
        <div class="score">{{ myRank.score }} 分</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { useLeaderboardStore } from '@/stores/leaderboard';
import { useUserStore } from '@/stores/user';

const props = defineProps<{
  gameType: string;
}>();

const userStore = useUserStore();
const leaderboardStore = useLeaderboardStore();

const currentUserId = computed(() => userStore.userId);
const currentType = computed(() => leaderboardStore.currentType);
const currentScope = computed(() => leaderboardStore.currentScope);
const leaderboard = computed(() => leaderboardStore.leaderboard);
const myRank = computed(() => leaderboardStore.myRank);
const loading = computed(() => leaderboardStore.loading);

const leaderboardTypes = [
  { value: 'rating', label: '评分榜' },
  { value: 'win_streak', label: '连胜榜' },
  { value: 'win_rate', label: '胜率榜' },
  { value: 'playtime', label: '时长榜' }
];

const scopes = [
  { value: 'global', label: '全球' },
  { value: 'weekly', label: '周榜' },
  { value: 'monthly', label: '月榜' }
];

function getRankClass(rank: number): string {
  if (rank === 1) return 'gold';
  if (rank === 2) return 'silver';
  if (rank === 3) return 'bronze';
  return '';
}

function changeType(type: string) {
  leaderboardStore.fetchLeaderboard(type, props.gameType, currentScope.value, currentUserId.value);
}

function changeScope(scope: string) {
  leaderboardStore.fetchLeaderboard(currentType.value, props.gameType, scope, currentUserId.value);
}

onMounted(() => {
  leaderboardStore.fetchLeaderboard('rating', props.gameType, 'global', currentUserId.value);
});
</script>
```

---

## 5. 完整流程示例：游戏结束结算

### 5.1 服务端自动处理流程

```
游戏结束 → GameEndProcessor.processGameEnd()
  ├── 1. 验证请求
  ├── 2. 计算 ELO 评分变化 (EloRatingCalculator)
  ├── 3. 计算奖励 (RewardConfig)
  │     ├── 基础奖励
  │     ├── 胜利/失败奖励
  │     ├── 连胜奖励
  │     └── 首胜奖励
  ├── 4. 发放货币 (CurrencyManager)
  ├── 5. 更新统计数据
  ├── 6. 检查成就 (AchievementManager)
  ├── 7. 更新排行榜 (LeaderboardManager)
  └── 8. 返回结算结果
```

### 5.2 客户端处理流程

```typescript
// 游戏结束时
async function handleGameEnd(gameResult: GameEndResponse) {
  if (!gameResult.success) {
    showToast('结算失败：' + gameResult.error_message);
    return;
  }

  const settlement = gameResult.settlements[0]; // 当前玩家

  // 1. 显示结算界面
  showSettlementDialog({
    result: settlement.result,
    ratingChange: settlement.rating_change,
    tierChanged: settlement.tier_changed,
    tierPromoted: settlement.tier_promoted,
    rewards: settlement.reward
  });

  // 2. 更新本地货币余额
  await currencyStore.refreshAfterGame(userId);

  // 3. 显示解锁的成就
  if (settlement.achievements_unlocked.length > 0) {
    showAchievementUnlocked(settlement.achievements_unlocked);
  }

  // 4. 刷新排行榜（如果需要）
  leaderboardStore.fetchMyRank('rating', 'gomoku', userId);
}
```

---

## 6. 成就系统详细说明

### 6.1 成就系统概述

成就系统在游戏结束时自动检测并解锁。系统支持：
- **25+ 种成就**：涵盖胜利、连胜、评分、等级、隐藏成就等
- **4 种稀有度**：普通、稀有、史诗、传说
- **自动触发**：游戏结算时自动检查成就条件
- **多类型奖励**：金币、宝石、荣誉、经验值

### 6.2 成就稀有度与图标

| 稀有度 | 英文 | 图标 | 颜色 | 点数范围 |
|--------|------|------|------|----------|
| 普通 | Common | ⚪ | #AAAAAA | 5-20 |
| 稀有 | Rare | 🔵 | #5555FF | 20-40 |
| 史诗 | Epic | 🟣 | #AA00FF | 40-70 |
| 传说 | Legendary | 🟡 | #FFAA00 | 70-100 |

### 6.3 完整成就列表

#### 6.3.1 胜利成就（里程碑型）

| 成就ID | 名称 | 描述 | 稀有度 | 点数 | 触发条件 | 奖励 |
|--------|------|------|--------|------|----------|------|
| `first_win` | 初出茅庐 | 获得第一场胜利 | 普通 | 10 | `total_wins >= 1` | 100金币, 10经验 |
| `win_10_games` | 初露锋芒 | 累计赢得10场游戏 | 普通 | 15 | `total_wins >= 10` | 200金币, 20荣誉, 20经验 |
| `win_50_games` | 小有成就 | 累计赢得50场游戏 | 稀有 | 25 | `total_wins >= 50` | 500金币, 5宝石, 30荣誉, 50经验 |
| `win_100_games` | 棋坛新秀 | 累计赢得100场游戏 | 史诗 | 50 | `total_wins >= 100` | 2000金币, 10宝石, 50荣誉, 100经验 |
| `win_500_games` | 棋坛大师 | 累计赢得500场游戏 | 史诗 | 70 | `total_wins >= 500` | 5000金币, 30宝石, 100荣誉, 200经验 |
| `win_1000_games` | 棋圣 | 累计赢得1000场游戏 | 传说 | 100 | `total_wins >= 1000` | 10000金币, 50宝石, 200荣誉, 500经验 |

#### 6.3.2 连胜成就（进度型）

| 成就ID | 名称 | 描述 | 稀有度 | 点数 | 触发条件 | 奖励 |
|--------|------|------|--------|------|----------|------|
| `win_streak_3` | 小试牛刀 | 连续赢得3场游戏 | 普通 | 15 | `current_win_streak >= 3` | 150金币, 15经验 |
| `win_streak_5` | 势如破竹 | 连续赢得5场游戏 | 稀有 | 30 | `current_win_streak >= 5` | 300金币, 20荣誉, 30经验 |
| `win_streak_10` | 所向披靡 | 连续赢得10场游戏 | 史诗 | 50 | `current_win_streak >= 10` | 500金币, 3宝石, 50荣誉, 50经验 |
| `win_streak_20` | 无人能挡 | 连续赢得20场游戏 | 传说 | 100 | `current_win_streak >= 20` | 2000金币, 10宝石, 100荣誉, 100经验 |

#### 6.3.3 评分成就

| 成就ID | 名称 | 描述 | 稀有度 | 点数 | 触发条件 | 奖励 |
|--------|------|------|--------|------|----------|------|
| `rating_1200` | 初露锋芒 | 评分达到1200 | 普通 | 10 | `current_rating >= 1200` | 100金币, 10经验 |
| `rating_1500` | 黄金之路 | 评分达到1500 | 稀有 | 30 | `current_rating >= 1500` | 300金币, 2宝石, 30经验 |
| `rating_1800` | 白金殿堂 | 评分达到1800 | 稀有 | 40 | `current_rating >= 1800` | 500金币, 5宝石, 40经验 |
| `rating_2000` | 钻石荣光 | 评分达到2000 | 史诗 | 50 | `current_rating >= 2000` | 800金币, 8宝石, 50荣誉, 50经验 |
| `rating_2500` | 大师境界 | 评分达到2500 | 史诗 | 70 | `current_rating >= 2500` | 1500金币, 15宝石, 80荣誉, 70经验 |
| `rating_3000` | 王者之巅 | 评分达到3000 | 传说 | 100 | `current_rating >= 3000` | 5000金币, 30宝石, 150荣誉, 100经验 |

#### 6.3.4 游戏场次成就

| 成就ID | 名称 | 描述 | 稀有度 | 点数 | 触发条件 | 奖励 |
|--------|------|------|--------|------|----------|------|
| `games_100` | 勤学苦练 | 完成100场对局 | 普通 | 15 | `total_games >= 100` | 200金币, 20荣誉, 15经验 |
| `games_500` | 棋艺精进 | 完成500场对局 | 稀有 | 30 | `total_games >= 500` | 500金币, 5宝石, 40荣誉, 30经验 |
| `games_1000` | 棋道修行 | 完成1000场对局 | 史诗 | 50 | `total_games >= 1000` | 1000金币, 10宝石, 60荣誉, 50经验 |

#### 6.3.5 等级成就

| 成就ID | 名称 | 描述 | 稀有度 | 点数 | 触发条件 | 奖励 |
|--------|------|------|--------|------|----------|------|
| `level_10` | 初窥门径 | 达到10级 | 普通 | 10 | `level >= 10` | 100金币, 10经验 |
| `level_25` | 棋手认证 | 达到25级 | 稀有 | 30 | `level >= 25` | 300金币, 3宝石, 30经验 |
| `level_50` | 棋艺大师 | 达到50级 | 史诗 | 50 | `level >= 50` | 600金币, 8宝石, 40荣誉, 50经验 |
| `level_100` | 传奇棋手 | 达到100级 | 传说 | 100 | `level >= 100` | 2000金币, 20宝石, 100荣誉, 100经验 |

#### 6.3.6 隐藏成就（特殊条件）

| 成就ID | 名称 | 描述 | 稀有度 | 点数 | 触发条件 |
|--------|------|------|--------|------|----------|
| `perfect_game` | 完美对局 | 在30步内获胜 | 传说 | 80 | `last_game_moves <= 30 AND last_game_result == WIN` |
| `comeback_king` | 绝地反击 | 评分劣势200+时获胜 | 传说 | 100 | `was_underdog == true AND last_game_result == WIN` |
| `quick_win` | 速战速决 | 5分钟内获胜 | 史诗 | 50 | `last_game_duration <= 300 AND last_game_result == WIN` |
| `endurance` | 持久战 | 对局超过30分钟 | 稀有 | 30 | `last_game_duration >= 1800` |

> **重要说明：GameResult 枚举值**
>
> 条件表达式中的 `last_game_result` 使用 `GameResult` 枚举的整数值：
> - `WIN = 0` - 获胜
> - `LOSS = 1` - 失败
> - `DRAW = 2` - 平局
> - `SURRENDER = 3` - 认输
> - `TIMEOUT = 4` - 超时
> - `DISCONNECT = 5` - 断线
>
> 因此，检查"获胜"的条件应写作 `last_game_result == 0`（而非 `== 1`）。
> 上表中使用 `WIN` 是为了可读性，实际代码中应使用数值 `0`。

### 6.4 成就触发机制

#### 6.4.1 触发时机

成就检查在 **游戏结算时自动触发**：

```
游戏结束 → GameEndProcessor.processGameEnd()
    │
    ├── 1-4. 计算 ELO 变化、发放奖励、更新统计
    │
    ├── 5. 检查成就 (AchievementManager.checkAndUnlock())
    │     │
    │     ├── 构建成就上下文 (AchievementContext)
    │     │     │
    │     │     ├── ★ 数据来源：PlayerSettlement（更新后的数据）
    │     │     │   而非从数据库读取旧值，确保首局胜利能正确触发成就
    │     │     │
    │     │     ├── total_games, total_wins, total_losses, total_draws
    │     │     ├── current_rating, peak_rating
    │     │     ├── current_win_streak, best_win_streak
    │     │     ├── level, experience_points
    │     │     └── last_game_result, last_game_moves, last_game_duration
    │     │
    │     ├── 遍历所有成就定义
    │     │
    │     ├── 评估条件表达式
    │     │     ├── 比较运算: ==, !=, >, >=, <, <=
    │     │     └── 逻辑运算: AND, OR, NOT
    │     │
    │     └── 解锁满足条件的成就
    │           ├── 发放奖励（金币/宝石/荣誉/经验）
    │           ├── 记录解锁时间
    │           └── 返回解锁列表
    │
    └── 返回结算结果（包含 achievements_unlocked）
```

#### 6.4.2 客户端无需主动触发

**重要**：客户端不需要调用专门的成就检查 API。成就检查在服务端游戏结算流程中自动完成。

### 6.5 成就 API 接口

#### 6.5.1 获取用户成就列表

```http
GET /api/v1/gamedata/achievements/{user_id}
Authorization: Bearer {access_token}

Query Parameters:
- page: number - 页码（默认 1）
- limit: number - 每页数量（默认 20）
- sort_by: string - 排序字段（默认 created_at）
- sort_order: string - 排序方向（asc/desc，默认 desc）
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "items": [
      {
        "achievement_id": "ach_001",
        "achievement_type": "first_win",
        "title": "初出茅庐",
        "description": "获得第一场胜利",
        "points": 10
      },
      {
        "achievement_id": "ach_002",
        "achievement_type": "win_streak_3",
        "title": "小试牛刀",
        "description": "连续赢得3场游戏",
        "points": 15
      }
    ],
    "total_count": 8,
    "page": 1,
    "limit": 20,
    "total_pages": 1
  },
  "message": "User achievements retrieved successfully"
}
```

> **注意**：当前 API 返回的是用户已解锁的成就列表。如需获取完整成就定义（包含未解锁、进度、奖励信息），请联系后端暴露 `AchievementManager::getAllDefinitions()` 接口。

#### 6.5.2 成就数据结构

```typescript
interface AchievementData {
  user_id: string;
  total_achievements: number;     // 成就总数
  unlocked_count: number;         // 已解锁数量
  total_points: number;           // 总成就点数
  achievements: Achievement[];
}

interface Achievement {
  achievement_id: string;
  name: string;
  description: string;
  category: 'milestone' | 'progress' | 'hidden';
  rarity: 'common' | 'rare' | 'epic' | 'legendary';
  icon: string;
  points: number;
  unlocked: boolean;
  unlocked_at?: string;
  is_hidden?: boolean;
  progress?: {
    current: number;
    target: number;
    percentage: number;
  };
  rewards: {
    gold?: number;
    gem?: number;
    honor?: number;
    experience?: number;
  };
}
```

### 6.6 客户端实现示例

#### 6.6.1 API 封装

```typescript
// api/achievement.ts
import { http } from '@/utils/http';

export interface Achievement {
  achievement_id: string;
  name: string;
  description: string;
  category: string;
  rarity: 'common' | 'rare' | 'epic' | 'legendary';
  icon: string;
  points: number;
  unlocked: boolean;
  unlocked_at?: string;
  is_hidden?: boolean;
  progress?: {
    current: number;
    target: number;
    percentage: number;
  };
  rewards: {
    gold?: number;
    gem?: number;
    honor?: number;
    experience?: number;
  };
}

export interface AchievementListResult {
  user_id: string;
  total_achievements: number;
  unlocked_count: number;
  total_points: number;
  achievements: Achievement[];
}

export const achievementApi = {
  async getAchievements(
    userId: string,
    options: {
      gameType?: string;
      includeHidden?: boolean;
    } = {}
  ): Promise<AchievementListResult> {
    const params = new URLSearchParams();
    if (options.gameType) params.append('game_type', options.gameType);
    if (options.includeHidden) params.append('include_hidden', 'true');

    const response = await http.get(
      `/api/v1/gamedata/achievements/${userId}?${params.toString()}`
    );
    return response.data.data;
  }
};
```

#### 6.6.2 Pinia Store

```typescript
// stores/achievement.ts
import { defineStore } from 'pinia';
import { achievementApi, Achievement, AchievementListResult } from '@/api/achievement';

export const useAchievementStore = defineStore('achievement', {
  state: () => ({
    achievements: null as AchievementListResult | null,
    loading: false,
    error: null as string | null
  }),

  getters: {
    // 已解锁成就
    unlockedAchievements: (state) =>
      state.achievements?.achievements.filter(a => a.unlocked) ?? [],

    // 未解锁成就（非隐藏）
    lockedAchievements: (state) =>
      state.achievements?.achievements.filter(a => !a.unlocked && !a.is_hidden) ?? [],

    // 隐藏成就（已解锁的）
    unlockedHiddenAchievements: (state) =>
      state.achievements?.achievements.filter(a => a.is_hidden && a.unlocked) ?? [],

    // 按稀有度分组
    byRarity: (state) => {
      const grouped: Record<string, Achievement[]> = {
        common: [], rare: [], epic: [], legendary: []
      };
      state.achievements?.achievements.forEach(a => {
        grouped[a.rarity]?.push(a);
      });
      return grouped;
    },

    // 解锁进度
    unlockProgress: (state) => {
      if (!state.achievements) return 0;
      return (state.achievements.unlocked_count / state.achievements.total_achievements) * 100;
    }
  },

  actions: {
    async fetchAchievements(userId: string, gameType?: string) {
      this.loading = true;
      this.error = null;

      try {
        this.achievements = await achievementApi.getAchievements(userId, { gameType });
      } catch (e: any) {
        this.error = e.message;
      } finally {
        this.loading = false;
      }
    }
  }
});
```

#### 6.6.3 成就展示组件

```vue
<!-- components/AchievementList.vue -->
<template>
  <div class="achievement-list">
    <!-- 进度概览 -->
    <div class="overview">
      <div class="progress-ring">
        <span class="percentage">{{ unlockProgress.toFixed(0) }}%</span>
      </div>
      <div class="stats">
        <div class="stat">
          <span class="value">{{ achievements?.unlocked_count }}</span>
          <span class="label">已解锁</span>
        </div>
        <div class="stat">
          <span class="value">{{ achievements?.total_achievements }}</span>
          <span class="label">总成就</span>
        </div>
        <div class="stat">
          <span class="value">{{ achievements?.total_points }}</span>
          <span class="label">成就点</span>
        </div>
      </div>
    </div>

    <!-- 稀有度筛选 -->
    <div class="rarity-filter">
      <button
        v-for="rarity in rarities"
        :key="rarity.value"
        :class="['filter-btn', rarity.value, { active: activeRarity === rarity.value }]"
        @click="activeRarity = rarity.value"
      >
        {{ rarity.label }} ({{ byRarity[rarity.value]?.length || 0 }})
      </button>
    </div>

    <!-- 成就列表 -->
    <div class="achievements">
      <div
        v-for="achievement in filteredAchievements"
        :key="achievement.achievement_id"
        :class="['achievement-card', achievement.rarity, { unlocked: achievement.unlocked }]"
      >
        <div class="icon">{{ achievement.icon }}</div>
        <div class="info">
          <div class="name">
            {{ achievement.is_hidden && !achievement.unlocked ? '???' : achievement.name }}
          </div>
          <div class="description">
            {{ achievement.is_hidden && !achievement.unlocked ? '隐藏成就' : achievement.description }}
          </div>
          <!-- 进度条 -->
          <div v-if="!achievement.unlocked && achievement.progress" class="progress">
            <div class="progress-bar">
              <div
                class="progress-fill"
                :style="{ width: `${achievement.progress.percentage}%` }"
              />
            </div>
            <span class="progress-text">
              {{ achievement.progress.current }} / {{ achievement.progress.target }}
            </span>
          </div>
        </div>
        <div class="points">{{ achievement.points }}pt</div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue';
import { useAchievementStore } from '@/stores/achievement';

const achievementStore = useAchievementStore();

const achievements = computed(() => achievementStore.achievements);
const byRarity = computed(() => achievementStore.byRarity);
const unlockProgress = computed(() => achievementStore.unlockProgress);

const activeRarity = ref('all');

const rarities = [
  { value: 'all', label: '全部' },
  { value: 'common', label: '普通' },
  { value: 'rare', label: '稀有' },
  { value: 'epic', label: '史诗' },
  { value: 'legendary', label: '传说' }
];

const filteredAchievements = computed(() => {
  if (activeRarity.value === 'all') {
    return achievements.value?.achievements ?? [];
  }
  return byRarity.value[activeRarity.value] ?? [];
});
</script>
```

#### 6.6.4 成就解锁弹窗

```vue
<!-- components/AchievementUnlockPopup.vue -->
<template>
  <Transition name="achievement-popup">
    <div v-if="visible" class="achievement-popup-overlay" @click="close">
      <div class="achievement-popup" :class="achievement?.rarity">
        <div class="effect"></div>
        <div class="content">
          <div class="icon">{{ achievement?.icon }}</div>
          <div class="title">成就解锁！</div>
          <div class="name">{{ achievement?.name }}</div>
          <div class="description">{{ achievement?.description }}</div>
          <div class="rewards">
            <span v-if="achievement?.rewards.gold" class="reward gold">
              +{{ achievement.rewards.gold }} 金币
            </span>
            <span v-if="achievement?.rewards.gem" class="reward gem">
              +{{ achievement.rewards.gem }} 宝石
            </span>
            <span v-if="achievement?.rewards.honor" class="reward honor">
              +{{ achievement.rewards.honor }} 荣誉
            </span>
            <span v-if="achievement?.rewards.experience" class="reward exp">
              +{{ achievement.rewards.experience }} 经验
            </span>
          </div>
          <div class="points">{{ achievement?.points }} 成就点</div>
        </div>
      </div>
    </div>
  </Transition>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue';
import type { Achievement } from '@/api/achievement';

const props = defineProps<{
  achievement: Achievement | null;
  visible: boolean;
}>();

const emit = defineEmits<{
  close: [];
}>();

function close() {
  emit('close');
}

// 自动关闭
watch(() => props.visible, (val) => {
  if (val) {
    setTimeout(() => close(), 4000);
  }
});
</script>
```

### 6.7 成就系统触发流程总结

```
┌─────────────────────────────────────────────────────────────────┐
│                     游戏结束                                     │
│                                                                 │
│  WebSocket game_end 消息                                        │
│  └─→ settlement.achievements_unlocked: ["win_streak_3", ...]   │
│                                                                 │
│  客户端处理：                                                    │
│  1. 解析 achievements_unlocked 数组                             │
│  2. 显示成就解锁弹窗（每个成就一个）                              │
│  3. 播放解锁动画和音效                                           │
│  4. 更新本地成就缓存（后台）                                      │
└─────────────────────────────────────────────────────────────────┘
```

**关键点**：
- 成就检查在服务端自动完成，客户端无需调用检查 API
- 解锁的成就 ID 列表包含在游戏结算响应中
- 客户端只需展示解锁动画和更新缓存

---

## 7. 数据模型参考

### 6.1 用户游戏档案

```typescript
interface UserGameProfile {
  user_id: string;
  game_type: string;

  // 等级系统
  level: number;
  experience_points: number;
  experience_to_next_level: number;

  // 评分系统
  current_rating: number;
  peak_rating: number;
  tier: string;
  tier_level: number;

  // 统计数据
  total_games: number;
  wins: number;
  losses: number;
  draws: number;
  win_rate: number;
  current_win_streak: number;
  best_win_streak: number;

  // 时间数据
  total_playtime_seconds: number;
  last_played_at: string;
  created_at: string;
}
```

### 6.2 奖励结构

```typescript
interface Reward {
  experience: number;
  currency_rewards: CurrencyReward[];
  item_rewards?: ItemReward[];
}

interface CurrencyReward {
  type: string;  // gold, gem, honor
  amount: number;
  reason: string;
}
```

---

## 7. 错误处理

### 7.1 常见错误码

| 错误码 | 说明 | 处理建议 |
|--------|------|----------|
| `INSUFFICIENT_CURRENCY` | 货币不足 | 提示用户充值或获取更多货币 |
| `CURRENCY_LIMIT_EXCEEDED` | 超过货币上限 | 提示用户消费后再次获取 |
| `LEADERBOARD_NOT_FOUND` | 排行榜不存在 | 使用默认排行榜类型 |
| `USER_NOT_RANKED` | 用户未上榜 | 显示"未上榜"状态 |
| `INVALID_CURRENCY_TYPE` | 无效货币类型 | 检查请求参数 |

### 7.2 错误处理示例

```typescript
async function handleApiCall<T>(apiCall: () => Promise<T>): Promise<T | null> {
  try {
    return await apiCall();
  } catch (error: any) {
    const errorCode = error.response?.data?.error_code;

    switch (errorCode) {
      case 'INSUFFICIENT_CURRENCY':
        showToast('金币不足，请先充值');
        break;
      case 'LEADERBOARD_NOT_FOUND':
        showToast('排行榜数据暂未生成，请稍后再试');
        break;
      default:
        showToast('网络错误，请重试');
    }

    return null;
  }
}
```

---

## 8. 性能优化建议

### 8.1 缓存策略

```typescript
// 使用 SWR (Stale-While-Revalidate) 策略
const CACHE_TTL = {
  CURRENCY: 60 * 1000,      // 1分钟
  LEADERBOARD: 5 * 60 * 1000, // 5分钟
  USER_RANK: 2 * 60 * 1000   // 2分钟
};

async function fetchWithCache<T>(
  key: string,
  fetcher: () => Promise<T>,
  ttl: number
): Promise<T> {
  const cached = localStorage.getItem(key);
  if (cached) {
    const { data, timestamp } = JSON.parse(cached);
    if (Date.now() - timestamp < ttl) {
      return data;
    }
  }

  const data = await fetcher();
  localStorage.setItem(key, JSON.stringify({ data, timestamp: Date.now() }));
  return data;
}
```

### 8.2 请求优化

- 使用分页加载排行榜数据
- 避免频繁刷新货币余额
- 游戏结束时一次性获取所有更新数据

---

## 9. 测试清单

### 9.1 经济系统测试

- [ ] 查询货币余额正确显示
- [ ] 游戏结束后货币自动更新
- [ ] 货币变化动画效果正常
- [ ] 货币不足时正确提示

### 9.2 评分系统测试

- [ ] 段位图标和名称正确显示
- [ ] 评分变化计算正确
- [ ] 段位晋升/降级正确触发
- [ ] 段位进度条显示正确

### 9.3 排行榜测试

- [ ] 排行榜列表正确加载
- [ ] 用户自己的排名正确显示
- [ ] 排行榜类型切换正常
- [ ] 排行榜范围切换正常

---

## 10. 场景化操作指南

本节详细说明客户端在各种场景下应如何调用 API 和展示数据。

---

### 10.1 主页场景

**触发时机**：用户登录成功后进入主页

**需要展示的数据**：
- 当前在线用户数
- 进行中的对局数
- 当前用户快速信息（头像、昵称、段位）
- 快速入口（开始匹配、好友对战等）

#### 10.1.1 数据获取流程

```
用户进入主页
    │
    ├─→ 并行请求 ─────────────────────────────────────────┐
    │                                                       │
    │   GET /api/v1/user/{user_id}/profile                 │
    │   └─→ 获取用户基础信息（昵称、头像）                   │
    │                                                       │
    │   GET /api/v1/gamedata/profiles/{user_id}            │
    │   └─→ 获取游戏档案（段位、评分、等级）                  │
    │                                                       │
    │   GET /api/v1/gamedata/currency/{user_id}            │
    │   └─→ 获取货币余额（金币、宝石、荣誉）                  │
    │                                                       │
    │   GET /api/v1/services/lists                         │
    │   └─→ 获取服务列表（在线状态、玩家数）                  │
    │                                                       │
    │   GET /api/v1/gomoku/server/status                   │
    │   └─→ 获取游戏服务器状态（进行中对局数）                │
    │                                                       │
    └───────────────────────────────────────────────────────┘
```

#### 10.1.2 API 调用详情

**获取服务状态（在线用户数）**：

```http
GET /api/v1/services/lists
Authorization: Bearer {access_token}
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "services": [
      {
        "name": "gomoku_server",
        "status": "healthy",
        "metadata": {
          "current_players": 156,
          "active_rooms": 42,
          "matching_players": 8
        }
      }
    ]
  }
}
```

**获取游戏服务器状态**：

```http
GET /api/v1/gomoku/server/status
Authorization: Bearer {access_token}
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "status": "running",
    "online_players": 156,
    "active_rooms": 42,
    "waiting_players": 8,
    "uptime_seconds": 86400
  }
}
```

#### 10.1.3 客户端实现

```typescript
// views/HomePage.vue
import { onMounted } from 'vue';
import { useUserStore } from '@/stores/user';
import { useGameProfileStore } from '@/stores/gameProfile';
import { useCurrencyStore } from '@/stores/currency';

const userStore = useUserStore();
const gameProfileStore = useGameProfileStore();
const currencyStore = useCurrencyStore();

// 服务器状态
const serverStatus = ref({
  onlinePlayers: 0,
  activeRooms: 0,
  waitingPlayers: 0
});

async function loadHomePageData() {
  const userId = userStore.userId;

  // 并行请求所有数据
  const [profile, gameProfile, currencies, status] = await Promise.all([
    userStore.fetchProfile(userId),
    gameProfileStore.fetchGameProfile(userId, 'gomoku'),
    currencyStore.fetchBalances(userId),
    fetchServerStatus()
  ]);

  serverStatus.value = status;
}

async function fetchServerStatus() {
  const response = await http.get('/api/v1/gomoku/server/status');
  return response.data.data;
}

onMounted(() => {
  loadHomePageData();
});
```

---

### 10.2 个人中心场景

**触发时机**：用户点击"个人中心"或头像

**需要展示的数据**：
- 用户基础信息（头像、昵称、ID）
- 段位信息（段位图标、评分、进度）
- 货币余额（金币、宝石、荣誉）
- 游戏统计（总场次、胜率、连胜）
- 成就列表（已解锁成就、进度）
- 每日统计（今日场次、今日收益）

#### 10.2.1 数据获取流程

```
用户点击个人中心
    │
    ├─→ 并行请求 ─────────────────────────────────────────┐
    │                                                       │
    │   GET /api/v1/user/{user_id}/profile                 │
    │   └─→ 用户基础信息                                    │
    │                                                       │
    │   GET /api/v1/gamedata/profiles/{user_id}            │
    │   └─→ 游戏档案（段位、评分、统计）                      │
    │                                                       │
    │   GET /api/v1/gamedata/currency/{user_id}            │
    │   └─→ 货币余额                                        │
    │                                                       │
    │   GET /api/v1/gamedata/achievements/{user_id}        │
    │   └─→ 成就列表                                        │
    │                                                       │
    │   GET /api/v1/gamedata/stats/daily/{user_id}         │
    │   └─→ 每日统计                                        │
    │                                                       │
    │   GET /api/v1/gamedata/leaderboard/rating/gomoku/rank/{user_id}
    │   └─→ 全球排名                                        │
    │                                                       │
    └───────────────────────────────────────────────────────┘
```

#### 10.2.2 API 调用详情

**获取用户游戏档案**：

```http
GET /api/v1/gamedata/profiles/{user_id}
Authorization: Bearer {access_token}
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "user_id": "usr_12345",
    "display_name": "棋手小明",
    "level": 15,
    "experience_points": 14500,
    "avatar_url": "https://example.com/avatar.png",
    "created_at": "2025-01-01T00:00:00Z",
    "last_login_at": "2025-02-21T10:30:00Z",
    "total_games": 189,
    "wins": 102,
    "losses": 78,
    "draws": 9,
    "current_rating": 1456,
    "peak_rating": 1520,
    "tier_level": 6,
    "current_win_streak": 3,
    "best_win_streak": 12
  },
  "message": "User profile retrieved successfully"
}
```

> **注意**：`tier_level` 是段位等级（0-15），对应关系：
> - 0-2: 青铜 III/I
> - 3-5: 白银 III/I
> - 6-8: 黄金 III/I
> - 9-11: 铂金 III/I
> - 12-14: 钻石 III/I
> - 15: 大师

**获取用户成就列表**：

```http
GET /api/v1/gamedata/achievements/{user_id}
Authorization: Bearer {access_token}

Query Parameters:
- game_type: string - 游戏类型（可选）
- category: string - 成就分类（可选）
- include_progress: boolean - 是否包含进度（默认 true）
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "user_id": "usr_12345",
    "total_achievements": 45,
    "unlocked_count": 12,
    "total_points": 1200,
    "achievements": [
      {
        "achievement_id": "first_win",
        "name": "初出茅庐",
        "description": "获得第一场胜利",
        "category": "gameplay",
        "icon": "🏆",
        "points": 50,
        "unlocked": true,
        "unlocked_at": "2025-01-15T08:00:00Z"
      },
      {
        "achievement_id": "win_streak_10",
        "name": "十连胜",
        "description": "累计获得10连胜",
        "category": "gameplay",
        "icon": "🔥",
        "points": 200,
        "unlocked": false,
        "progress": {
          "current": 7,
          "target": 10,
          "percentage": 70
        }
      }
    ]
  }
}
```

**获取每日统计**：

```http
GET /api/v1/gamedata/stats/daily/{user_id}
Authorization: Bearer {access_token}

Query Parameters:
- game_type_id: number - 游戏类型ID（可选，默认 1）
- date: string - 日期 YYYY-MM-DD（可选，默认今天）
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "user_id": "usr_12345",
    "game_type_id": 1,
    "stat_date": "2025-02-21",
    "games_played": 8,
    "games_won": 5,
    "games_lost": 2,
    "games_drawn": 1,
    "first_win_claimed": true,
    "total_playtime_seconds": 2400,
    "total_gold_earned": 450,
    "total_honor_earned": 50,
    "total_exp_earned": 800,
    "rating_change": 28,
    "peak_streak": 3
  },
  "message": "Daily stats retrieved successfully"
}
```

#### 10.2.3 客户端实现

```typescript
// views/ProfilePage.vue
import { onMounted, computed } from 'vue';
import { useUserStore } from '@/stores/user';
import { useGameProfileStore } from '@/stores/gameProfile';
import { useCurrencyStore } from '@/stores/currency';
import { useAchievementStore } from '@/stores/achievement';
import { getTierFromRating, TierInfo } from '@/utils/tier';

const userStore = useUserStore();
const gameProfileStore = useGameProfileStore();
const currencyStore = useCurrencyStore();
const achievementStore = useAchievementStore();

// 每日统计
const dailyStats = ref(null);

// 计算段位信息
const tierInfo = computed(() => {
  const rating = gameProfileStore.currentRating;
  const tier = getTierFromRating(rating);
  return TierInfo[tier];
});

async function loadProfileData() {
  const userId = userStore.userId;

  await Promise.all([
    gameProfileStore.fetchGameProfile(userId, 'gomoku'),
    currencyStore.fetchBalances(userId),
    achievementStore.fetchAchievements(userId),
    fetchDailyStats(userId)
  ]);
}

async function fetchDailyStats(userId: string) {
  const response = await http.get(
    `/api/v1/gamedata/stats/daily/${userId}?game_type=gomoku`
  );
  dailyStats.value = response.data.data;
}

onMounted(() => {
  loadProfileData();
});
```

---

### 10.3 游戏大厅场景

**触发时机**：用户点击"开始游戏"进入游戏大厅

**需要展示的数据**：
- 房间列表（可加入的房间）
- 匹配按钮（快速匹配）
- 当前匹配状态（匹配中、等待人数）
- 排行榜预览（前10名）

#### 10.3.1 数据获取流程

```
用户进入游戏大厅
    │
    ├─→ 初始化 WebSocket 连接 ─────────────────────────────┐
    │   ws://{host}:{port}/ws/gomoku/game?token={jwt}      │
    │                                                       │
    ├─→ 并行请求 ─────────────────────────────────────────┤
    │                                                       │
    │   GET /api/v1/gomoku/rooms                           │
    │   └─→ 可加入的房间列表                                │
    │                                                       │
    │   GET /api/v1/gomoku/match/pool                      │
    │   └─→ 匹配池状态                                      │
    │                                                       │
    │   GET /api/v1/gamedata/leaderboard/rating/gomoku?limit=10
    │   └─→ 排行榜前10名                                    │
    │                                                       │
    └───────────────────────────────────────────────────────┘
```

#### 10.3.2 API 调用详情

**获取房间列表**：

```http
GET /api/v1/gomoku/rooms
Authorization: Bearer {access_token}

Query Parameters:
- status: string - 房间状态（waiting|playing|all）
- mode: string - 游戏模式（ranked|casual|friendly）
- offset: number - 偏移量
- limit: number - 数量限制
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "rooms": [
      {
        "room_id": "room_abc123",
        "name": "高手切磋",
        "host_id": "usr_001",
        "host_name": "棋圣",
        "mode": "ranked",
        "status": "waiting",
        "current_players": 1,
        "max_players": 2,
        "time_control": "10+5",
        "created_at": "2025-02-21T10:30:00Z"
      }
    ],
    "total": 42,
    "offset": 0,
    "limit": 20
  }
}
```

**获取匹配池状态**：

```http
GET /api/v1/gomoku/match/pool
Authorization: Bearer {access_token}
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "total_players": 28,
    "ranked_players": 15,
    "casual_players": 13,
    "average_wait_time_seconds": 12,
    "my_status": "idle"
  }
}
```

#### 10.3.3 匹配操作

**开始匹配**：

```http
POST /api/v1/gomoku/match/start
Authorization: Bearer {access_token}
Content-Type: application/json

{
  "mode": "ranked",
  "preferences": {
    "rating_range": 200,
    "time_control": "10+5"
  }
}
```

**响应示例**：

```json
{
  "success": true,
  "data": {
    "match_request_id": "mr_xyz789",
    "status": "matching",
    "estimated_wait_seconds": 15
  }
}
```

**取消匹配**：

```http
POST /api/v1/gomoku/match/cancel
Authorization: Bearer {access_token}
```

---

### 10.4 对局中场景

**触发时机**：用户进入对局（通过 WebSocket）

**需要展示的数据**：
- 对手信息（头像、昵称、段位）
- 对局时间
- 落子统计
- 聊天消息

#### 10.4.1 对局信息获取

对局信息主要通过 WebSocket 实时获取，但可以在进入对局前获取对手信息：

```
用户匹配成功，准备进入对局
    │
    ├─→ 获取对手信息 ──────────────────────────────────────┐
    │                                                       │
    │   GET /api/v1/user/{opponent_id}/profile             │
    │   └─→ 对手基础信息                                    │
    │                                                       │
    │   GET /api/v1/gamedata/profiles/{opponent_id}        │
    │   └─→ 对手游戏档案（段位、评分）                        │
    │                                                       │
    │   GET /api/v1/gamedata/stats/daily/{opponent_id}     │
    │   └─→ 对手今日统计（可选）                             │
    │                                                       │
    └───────────────────────────────────────────────────────┘
```

#### 10.4.2 WebSocket 消息处理

```typescript
// 对局中的 WebSocket 消息
interface GameMessage {
  type: 'game_start' | 'move' | 'game_end' | 'chat' | 'state_update';
  data: any;
}

// 游戏开始消息
interface GameStartMessage {
  type: 'game_start';
  data: {
    game_id: string;
    players: [
      {
        user_id: string;
        color: 'black' | 'white';
        rating: number;
        tier: string;
        display_name: string;
        avatar_url: string;
      }
    ];
    time_control: {
      initial_seconds: number;
      increment_seconds: number;
    };
    started_at: string;
  };
}

// 游戏结束消息
interface GameEndMessage {
  type: 'game_end';
  data: {
    game_id: string;
    result: 'win' | 'loss' | 'draw';
    winner_id: string;
    end_reason: 'normal' | 'surrender' | 'timeout' | 'disconnect';
    settlement: PlayerSettlement;  // 结算信息（见第5节）
  };
}
```

---

### 10.5 游戏结束场景

**触发时机**：对局结束（WebSocket 收到 game_end 消息）

**需要展示的数据**：
- 游戏结果（胜负、原因）
- 评分变化（评分变化、段位变化）
- 获得奖励（经验、货币）
- 成就解锁（新成就列表）
- 统计更新（连胜、总场次）

#### 10.5.1 完整处理流程

```
收到 game_end WebSocket 消息
    │
    ├─→ 1. 解析结算数据
    │   └─→ settlement 数据已包含在 WebSocket 消息中
    │
    ├─→ 2. 显示结算界面
    │   ├─→ 游戏结果动画
    │   ├─→ 评分变化动画
    │   ├─→ 段位变化特效（如有）
    │   ├─→ 奖励展示
    │   └─→ 成就解锁弹窗
    │
    ├─→ 3. 更新本地缓存（可选）
    │   ├─→ GET /api/v1/gamedata/currency/{user_id}
    │   ├─→ GET /api/v1/gamedata/profiles/{user_id}
    │   └─→ GET /api/v1/gamedata/achievements/{user_id}
    │
    └─→ 4. 返回大厅
```

#### 10.5.2 客户端实现

```typescript
// 处理游戏结束
async function handleGameEnd(message: GameEndMessage) {
  const { settlement } = message.data;

  // 1. 显示结算界面
  showSettlementDialog({
    // 游戏结果
    result: settlement.result,
    endReason: message.data.end_reason,

    // 评分变化
    ratingBefore: settlement.rating_before,
    ratingAfter: settlement.rating_after,
    ratingChange: settlement.rating_change,

    // 段位变化
    tierBefore: settlement.tier_before,
    tierAfter: settlement.tier_after,
    tierChanged: settlement.tier_changed,
    tierPromoted: settlement.tier_promoted,
    tierDemoted: settlement.tier_demoted,

    // 奖励
    experience: settlement.reward.experience,
    gold: settlement.reward.getTotalGold(),
    gem: settlement.reward.getTotalGem(),
    honor: settlement.reward.getTotalHonor(),

    // 统计
    newWinStreak: settlement.new_win_streak,
    newGamesPlayed: settlement.new_games_played,

    // 成就
    achievementsUnlocked: settlement.achievements_unlocked
  });

  // 2. 播放音效和动画
  if (settlement.result === 'WIN') {
    playSound('victory');
    if (settlement.tier_promoted) {
      playSound('promotion');
      showPromotionEffect(settlement.tier_after);
    }
  } else if (settlement.result === 'LOSS') {
    playSound('defeat');
  }

  // 3. 显示成就解锁弹窗（如有）
  if (settlement.achievements_unlocked.length > 0) {
    for (const achievementId of settlement.achievements_unlocked) {
      await showAchievementUnlockPopup(achievementId);
    }
  }

  // 4. 更新本地缓存（后台执行）
  Promise.all([
    currencyStore.fetchBalances(userId),
    gameProfileStore.fetchGameProfile(userId, 'gomoku'),
    achievementStore.fetchAchievements(userId)
  ]);
}
```

---

### 10.6 排行榜场景

**触发时机**：用户点击"排行榜"

**需要展示的数据**：
- 排行榜列表（玩家排名、信息）
- 当前用户排名
- 排行榜类型切换（评分、连胜、胜率等）
- 排行榜范围切换（全球、周榜、月榜）

#### 10.6.1 数据获取流程

```
用户点击排行榜
    │
    ├─→ GET /api/v1/gamedata/leaderboard/{type}/{game_type}
    │   ?scope={scope}&offset=0&limit=50&user_id={user_id}
    │
    └─→ 响应包含：
        ├─→ entries: 排行榜列表
        └─→ my_rank: 当前用户排名
```

---

### 10.7 场景数据获取汇总

| 场景 | 需要调用的 API | 缓存策略 |
|------|---------------|---------|
| **主页** | `/user/profile`, `/gamedata/profiles`, `/gamedata/currency`, `/gomoku/server/status` | 缓存5分钟 |
| **个人中心** | `/user/profile`, `/gamedata/profiles`, `/gamedata/currency`, `/gamedata/achievements`, `/gamedata/stats/daily`, `/leaderboard/.../rank` | 缓存2分钟 |
| **游戏大厅** | `/gomoku/rooms`, `/gomoku/match/pool`, `/gamedata/leaderboard` | 实时刷新 |
| **对局中** | WebSocket 实时通信 | 无缓存 |
| **游戏结束** | WebSocket 结算数据（已包含所有信息） | 结算后刷新缓存 |
| **排行榜** | `/gamedata/leaderboard` | 缓存5分钟 |

---

## 11. 完整 API 端点清单

### 11.1 Auth Service（端口 8083）

| 方法 | 路径 | 功能 |
|------|------|------|
| POST | `/api/v1/auth/login` | 用户登录 |
| POST | `/api/v1/auth/register` | 用户注册 |
| POST | `/api/v1/auth/refresh` | 刷新令牌 |
| POST | `/api/v1/auth/validate` | 验证令牌 |
| POST | `/api/v1/auth/logout` | 用户登出 |

### 11.2 User Service（端口 8082）

| 方法 | 路径 | 功能 |
|------|------|------|
| GET | `/api/v1/user/{user_id}` | 获取用户信息 |
| GET | `/api/v1/user/{user_id}/profile` | 获取用户档案 |
| PUT | `/api/v1/user/{user_id}/profile` | 更新用户档案 |
| PUT | `/api/v1/user/{user_id}/online-status` | 更新在线状态 |
| GET | `/api/v1/user/by-username/{username}` | 按用户名查询 |

### 11.3 Game Data Service（端口 8084）

| 方法 | 路径 | 功能 |
|------|------|------|
| **用户档案** |||
| GET | `/api/v1/gamedata/profiles/{user_id}` | 获取游戏档案 |
| PUT | `/api/v1/gamedata/profiles/{user_id}` | 更新游戏档案 |
| **货币系统** |||
| GET | `/api/v1/gamedata/currency/{user_id}` | 获取所有货币 |
| GET | `/api/v1/gamedata/currency/{user_id}/{type}` | 获取指定货币 |
| PUT | `/api/v1/gamedata/currency` | 更新货币 |
| **成就系统** |||
| GET | `/api/v1/gamedata/achievements/{user_id}` | 获取成就列表 |
| POST | `/api/v1/gamedata/achievements` | 解锁成就 |
| **排行榜** |||
| GET | `/api/v1/gamedata/leaderboard/{type}/{game}` | 获取排行榜 |
| GET | `/api/v1/gamedata/leaderboard/{type}/{game}/rank/{user_id}` | 获取用户排名 |
| **游戏结算** |||
| POST | `/api/v1/gamedata/settlement` | 游戏结算 |
| GET | `/api/v1/gamedata/settlement/{game_id}` | 获取结算记录 |
| GET | `/api/v1/gamedata/stats/daily/{user_id}` | 获取每日统计 |
| **库存管理** |||
| GET | `/api/v1/gamedata/inventory/{user_id}` | 获取库存 |
| POST | `/api/v1/gamedata/inventory/{user_id}/use` | 使用物品 |

### 11.4 Gomoku Server（端口 8085）

| 方法 | 路径 | 功能 |
|------|------|------|
| **房间管理** |||
| GET | `/api/v1/gomoku/rooms` | 获取房间列表 |
| POST | `/api/v1/gomoku/rooms` | 创建房间 |
| GET | `/api/v1/gomoku/rooms/{room_id}` | 获取房间信息 |
| POST | `/api/v1/gomoku/rooms/{room_id}/join` | 加入房间 |
| POST | `/api/v1/gomoku/rooms/{room_id}/leave` | 离开房间 |
| POST | `/api/v1/gomoku/rooms/{room_id}/start` | 开始游戏 |
| **匹配系统** |||
| POST | `/api/v1/gomoku/match/start` | 开始匹配 |
| POST | `/api/v1/gomoku/match/cancel` | 取消匹配 |
| GET | `/api/v1/gomoku/match/status` | 获取匹配状态 |
| GET | `/api/v1/gomoku/match/pool` | 获取匹配池状态 |
| **服务器状态** |||
| GET | `/api/v1/gomoku/server/status` | 服务器状态 |
| **WebSocket** |||
| WS | `/ws/gomoku/game?token={jwt}` | 游戏实时通信 |

### 11.5 Service Registry（端口 8090）

| 方法 | 路径 | 功能 |
|------|------|------|
| GET | `/api/v1/services/lists` | 获取服务列表 |
| GET | `/api/v1/services/recommend` | 获取推荐服务 |

---

## 12. TypeScript 类型定义汇总

```typescript
// ==================== 用户相关 ====================

interface UserProfile {
  user_id: string;
  username: string;
  nickname: string;
  email: string;
  avatar_url: string;
  status: 'ACTIVE' | 'SUSPENDED' | 'BANNED';
  online_status: 'OFFLINE' | 'ONLINE' | 'IN_GAME' | 'AWAY';
  created_at: string;
  last_login_at: string;
}

// ==================== 游戏档案（服务端实际返回） ====================

interface UserGameProfile {
  user_id: string;
  display_name: string;
  level: number;
  experience_points: number;
  avatar_url: string;
  created_at: string;
  last_login_at: string;
  // 游戏统计
  total_games: number;
  wins: number;
  losses: number;
  draws: number;
  // 评分相关
  current_rating: number;
  peak_rating: number;
  tier_level: number;           // 段位等级 (0-15)
  current_win_streak: number;
  best_win_streak: number;
}

// ==================== 货币系统（服务端实际返回） ====================

// 单种货币响应
interface CurrencyResponse {
  user_id: string;
  currency_type: string;  // "gold", "gem", "honor"
  amount: number;
}

// 所有货币响应
interface AllCurrenciesResponse {
  user_id: string;
  currencies: Array<{
    currency_type: string;
    amount: number;
  }>;
  total_types: number;
}

// 便捷类型（客户端使用）
interface CurrencyBalance {
  gold: number;
  gem: number;
  honor: number;
}

// ==================== 成就系统（服务端实际返回） ====================

// 成就列表响应
interface AchievementListResponse {
  items: Achievement[];
  total_count: number;
  page: number;
  limit: number;
  total_pages: number;
}

interface Achievement {
  achievement_id: string;
  achievement_type: string;  // "first_win", "win_streak_3", etc.
  title: string;
  description: string;
  points: number;
}

// ==================== 排行榜 ====================

interface LeaderboardEntry {
  rank: number;
  user_id: string;
  display_name: string;
  score: number;
  tier_name: string;
  win_rate: number;
  games_played: number;
  avatar_url: string;
}

interface LeaderboardResult {
  type: string;
  scope: string;
  updated_at: string;
  total_players: number;
  entries: LeaderboardEntry[];
  my_rank?: LeaderboardEntry;
}

// ==================== 游戏结算 ====================

interface PlayerSettlement {
  user_id: string;
  result: 'WIN' | 'LOSS' | 'DRAW' | 'SURRENDER' | 'TIMEOUT';
  rating_before: number;
  rating_after: number;
  rating_change: number;
  tier_before: string;
  tier_after: string;
  tier_changed: boolean;
  tier_promoted: boolean;
  tier_demoted: boolean;
  reward: {
    experience: number;
    currencies: Array<{ type: string; amount: number }>;
  };
  // 更新后的统计数据（用于成就检查）
  new_win_streak: number;
  new_best_win_streak: number;
  new_games_played: number;
  new_games_today: number;
  new_wins: number;
  new_losses: number;
  new_draws: number;
  new_level: number;
  achievements_unlocked: string[];
}

// ==================== 每日统计（服务端实际返回） ====================

interface DailyStats {
  user_id: string;
  game_type_id: number;
  stat_date: string;           // YYYY-MM-DD
  games_played: number;
  games_won: number;
  games_lost: number;
  games_drawn: number;
  first_win_claimed: boolean;
  total_playtime_seconds: number;
  total_gold_earned: number;
  total_honor_earned: number;
  total_exp_earned: number;
  rating_change: number;
  peak_streak: number;
}

// ==================== 房间信息 ====================

interface GameRoom {
  room_id: string;
  name: string;
  host_id: string;
  host_name: string;
  mode: 'ranked' | 'casual' | 'friendly';
  status: 'waiting' | 'playing' | 'finished';
  current_players: number;
  max_players: number;
  time_control: string;
  created_at: string;
}

// ==================== 匹配状态 ====================

interface MatchStatus {
  match_request_id: string;
  status: 'idle' | 'matching' | 'matched' | 'cancelled';
  wait_time_seconds?: number;
  estimated_wait_seconds?: number;
  matched_room_id?: string;
}
```

---

**文档版本**: 2.3.0
**创建日期**: 2025-02-21
**更新日期**: 2026-03-01
**适用版本**: Game Data Service v1.0.0
**维护状态**: ✅ 服务端已完工，可直接集成

## 版本历史

| 版本 | 日期 | 变更内容 |
|------|------|----------|
| 2.3.0 | 2026-03-01 | **重要更新**：API 响应格式对齐服务端实际实现 1) 货币 API：`balance`→`amount`，返回 `currencies` 数组 2) 成就 API：返回 `items` 数组和分页信息 3) 用户档案 API：对齐 `UserGameProfile` 字段 4) 每日统计 API：字段名对齐 `total_*` 前缀 5) TypeScript 类型定义更新 |
| 2.2.0 | 2026-03-01 | 修复排行榜 API：1) handleGetLeaderboard 使用 LeaderboardManager 2) 返回完整字段（display_name, tier_name, win_rate, avatar_url）3) 添加 my_rank 支持 4) 更新获取用户排名 API 响应格式 |
| 2.1.0 | 2026-03-01 | 修复成就系统：1) 添加 GameResult 枚举值说明（WIN=0）2) 修复成就上下文数据来源说明 3) 补充 PlayerSettlement 完整字段定义 |
| 2.0.0 | 2025-02-21 | 添加场景化操作指南、完整 API 列表、TypeScript 类型定义 |
| 1.0.0 | 2025-02-21 | 初始版本，包含经济/评分/排行榜/成就系统集成指南 |
