<template>
  <div class="leaderboard-page">
    <!-- 顶部导航栏 -->
    <div class="top-nav-bar">
      <el-button text @click="goBack" class="back-btn">
        <el-icon><ArrowLeft /></el-icon>
        返回首页
      </el-button>
      <div class="nav-spacer"></div>
    </div>

    <!-- 排行榜内容 -->
    <div class="leaderboard-container">
      <el-card class="leaderboard-card">
        <template #header>
          <div class="card-header">
            <el-icon><Trophy /></el-icon>
            <h1>{{ t('nav.leaderboard') }}</h1>
          </div>
        </template>
        <el-tabs v-model="activeTab" @tab-change="loadLeaderboard">
          <el-tab-pane label="全球排行" name="global">
            <el-table :data="leaderboard" v-loading="loading">
              <el-table-column prop="rank" label="排名" width="80">
                <template #default="{ row }">
                  <el-tag :type="getRankType(row.rank)" effect="dark">
                    {{ row.rank }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column label="玩家">
                <template #default="{ row }">
                  <div class="user-info">
                    <el-avatar :size="32">
                      {{ row.username?.[0]?.toUpperCase() || '?' }}
                    </el-avatar>
                    <span>{{ row.username || row.userId }}</span>
                  </div>
                </template>
              </el-table-column>
              <el-table-column label="段位" width="120">
                <template #default="{ row }">
                  <TierBadge :tier="row.tier" :name="row.tierName" size="small" />
                </template>
              </el-table-column>
              <el-table-column prop="score" label="积分" />
              <el-table-column prop="wins" label="胜场" />
              <el-table-column prop="losses" label="负场" />
              <el-table-column label="胜率">
                <template #default="{ row }">
                  {{ row.winRate != null ? (row.winRate * 100).toFixed(1) + '%' : '-' }}
                </template>
              </el-table-column>
            </el-table>
          </el-tab-pane>
          <el-tab-pane label="周排行" name="weekly">
            <el-table :data="leaderboard" v-loading="loading">
              <el-table-column prop="rank" label="排名" width="80">
                <template #default="{ row }">
                  <el-tag :type="getRankType(row.rank)" effect="dark">
                    {{ row.rank }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column label="玩家">
                <template #default="{ row }">
                  <div class="user-info">
                    <el-avatar :size="32">
                      {{ row.username?.[0]?.toUpperCase() || '?' }}
                    </el-avatar>
                    <span>{{ row.username || row.userId }}</span>
                  </div>
                </template>
              </el-table-column>
              <el-table-column label="段位" width="120">
                <template #default="{ row }">
                  <TierBadge :tier="row.tier" :name="row.tierName" size="small" />
                </template>
              </el-table-column>
              <el-table-column prop="score" label="积分" />
              <el-table-column prop="wins" label="胜场" />
              <el-table-column prop="losses" label="负场" />
              <el-table-column label="胜率">
                <template #default="{ row }">
                  {{ row.winRate != null ? (row.winRate * 100).toFixed(1) + '%' : '-' }}
                </template>
              </el-table-column>
            </el-table>
          </el-tab-pane>
          <el-tab-pane label="月排行" name="monthly">
            <el-table :data="leaderboard" v-loading="loading">
              <el-table-column prop="rank" label="排名" width="80">
                <template #default="{ row }">
                  <el-tag :type="getRankType(row.rank)" effect="dark">
                    {{ row.rank }}
                  </el-tag>
                </template>
              </el-table-column>
              <el-table-column label="玩家">
                <template #default="{ row }">
                  <div class="user-info">
                    <el-avatar :size="32">
                      {{ row.username?.[0]?.toUpperCase() || '?' }}
                    </el-avatar>
                    <span>{{ row.username || row.userId }}</span>
                  </div>
                </template>
              </el-table-column>
              <el-table-column label="段位" width="120">
                <template #default="{ row }">
                  <TierBadge :tier="row.tier" :name="row.tierName" size="small" />
                </template>
              </el-table-column>
              <el-table-column prop="score" label="积分" />
              <el-table-column prop="wins" label="胜场" />
              <el-table-column prop="losses" label="负场" />
              <el-table-column label="胜率">
                <template #default="{ row }">
                  {{ row.winRate != null ? (row.winRate * 100).toFixed(1) + '%' : '-' }}
                </template>
              </el-table-column>
            </el-table>
          </el-tab-pane>
        </el-tabs>

        <!-- 我的排名 -->
        <div class="my-rank-section" v-if="myRank">
          <div class="section-title">我的排名</div>
          <div class="my-rank-card">
            <div class="rank-number">#{{ myRank.rank }}</div>
            <div class="user-info">
              <el-avatar :size="40">
                {{ myRank.username?.[0]?.toUpperCase() || '?' }}
              </el-avatar>
              <div class="user-details">
                <div class="username">{{ myRank.username }}</div>
                <TierBadge :tier="myRank.tier" :name="myRank.tierName" size="small" />
              </div>
            </div>
            <div class="stats">
              <div class="score">{{ myRank.score }} 分</div>
              <div class="win-rate" v-if="myRank.winRate != null">
                胜率 {{ (myRank.winRate * 100).toFixed(1) }}%
              </div>
            </div>
          </div>
        </div>
      </el-card>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { ArrowLeft, Trophy } from '@element-plus/icons-vue'
import { leaderboardApi } from '@/api'
import type { LeaderboardScope, LeaderboardEntry } from '@/types'
import TierBadge from '@/components/common/TierBadge.vue'
import { getTierByRating, getTierInfo, tierNameToChinese, type Tier } from '@/types/tier.types'
import { useAuthStore } from '@/stores/auth.store'

interface DisplayEntry {
  rank: number
  userId: string
  username: string
  score: number
  tier: Tier
  tierName: string
  wins: number
  losses: number
  winRate: number | null
}

const router = useRouter()
const { t } = useI18n()
const authStore = useAuthStore()

const activeTab = ref<LeaderboardScope>('global')
const loading = ref(false)
const leaderboard = ref<DisplayEntry[]>([])
const myRank = ref<DisplayEntry | null>(null)

// 获取当前用户 ID
const currentUserId = computed(() => authStore.user?.id)

const goBack = () => {
  router.push('/')
}

const getRankType = (rank: number) => {
  if (rank === 1) return 'danger'
  if (rank === 2) return 'warning'
  if (rank === 3) return 'success'
  return 'info'
}

const transformEntry = (entry: LeaderboardEntry): DisplayEntry => {
  // 使用 tier 或根据 score 计算
  const tier = entry.tier !== undefined ? entry.tier as Tier : getTierByRating(entry.score)
  const tierInfo = getTierInfo(tier)
  return {
    rank: entry.rank,
    userId: entry.user_id,
    username: entry.display_name || entry.user_id.split('_')[1] || entry.user_id,
    score: entry.score,
    tier,
    // 服务端返回英文段位名称，转换为中文
    tierName: entry.tier_name ? tierNameToChinese(entry.tier_name) : tierInfo.name,
    wins: 0, // 新 API 不再提供单独的胜/负场次
    losses: 0,
    // 服务端返回 win_rate 为 0.0-1.0，直接使用
    winRate: entry.win_rate ?? null,
  }
}

const loadLeaderboard = async () => {
  loading.value = true
  try {
    // 传入 userId 以获取 my_rank
    const response = await leaderboardApi.getLeaderboard('rating', 'gomoku', {
      scope: activeTab.value,
      limit: 50,
      userId: currentUserId.value || undefined
    })
    leaderboard.value = (response.entries || []).map(transformEntry)

    // 处理用户自己的排名
    if (response.my_rank) {
      myRank.value = transformEntry(response.my_rank)
    } else {
      myRank.value = null
    }
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  loadLeaderboard()
})
</script>

<style scoped>
.leaderboard-page {
  min-height: 100vh;
  background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
  padding: 1.5rem;
}

/* 顶部导航栏 */
.top-nav-bar {
  max-width: 1000px;
  margin: 0 auto 1rem;
  display: flex;
  align-items: center;
  padding: 0.5rem 1rem;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 12px;
  border: 1px solid rgba(255, 255, 255, 0.1);
}

.back-btn {
  color: rgba(255, 255, 255, 0.8);
  font-size: 0.9rem;
}

.back-btn:hover {
  color: white;
}

.nav-spacer {
  flex: 1;
}

/* 排行榜容器 */
.leaderboard-container {
  max-width: 1000px;
  margin: 0 auto;
}

.leaderboard-card {
  background: rgba(255, 255, 255, 0.95);
  border-radius: 16px;
  border: none;
}

.leaderboard-card :deep(.el-card__header) {
  border-bottom: 1px solid #e5e7eb;
  padding: 1.25rem 1.5rem;
}

.card-header {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.card-header .el-icon {
  font-size: 1.5rem;
  color: #f59e0b;
}

.card-header h1 {
  margin: 0;
  font-size: 1.25rem;
  font-weight: 600;
  color: #1a1a2e;
}

.leaderboard-card :deep(.el-card__body) {
  padding: 1.5rem;
}

.user-info {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

/* 我的排名区域 */
.my-rank-section {
  margin-top: 1.5rem;
  padding-top: 1rem;
  border-top: 1px solid #e5e7eb;
}

.section-title {
  font-size: 0.875rem;
  color: #6b7280;
  margin-bottom: 0.75rem;
  font-weight: 500;
}

.my-rank-card {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 1rem;
  background: linear-gradient(135deg, #e0f2fe 0%, #bae6fd 100%);
  border-radius: 12px;
  border: 1px solid #0ea5e9;
}

.rank-number {
  font-size: 1.5rem;
  font-weight: 700;
  color: #0369a1;
  min-width: 60px;
  text-align: center;
}

.my-rank-card .user-info {
  flex: 1;
  gap: 0.75rem;
}

.user-details {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.username {
  font-weight: 600;
  color: #1e3a5f;
}

.my-rank-card .stats {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 0.25rem;
}

.my-rank-card .score {
  font-size: 1.125rem;
  font-weight: 600;
  color: #0369a1;
}

.my-rank-card .win-rate {
  font-size: 0.75rem;
  color: #6b7280;
}
</style>
