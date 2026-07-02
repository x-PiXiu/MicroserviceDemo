<template>
  <el-dialog
    v-model="visible"
    :title="dialogTitle"
    width="520px"
    :close-on-click-modal="false"
    :close-on-press-escape="false"
    class="settlement-modal"
    @close="handleClose"
  >
    <div class="settlement-content" v-if="settlement">
      <!-- 结果展示 -->
      <div class="result-section" :class="resultClass">
        <div class="result-icon">{{ resultIcon }}</div>
        <div class="result-text">{{ resultText }}</div>
      </div>

      <!-- 评分变化 -->
      <div class="rating-section">
        <div class="rating-header">
          <span class="rating-label">评分变化</span>
          <span class="rating-change" :class="ratingChangeClass">
            {{ ratingChangeText }}
          </span>
        </div>
        <div class="rating-detail">
          <span class="rating-before">{{ settlement.rating?.before }}</span>
          <span class="rating-arrow">→</span>
          <span class="rating-after">{{ settlement.rating?.after }}</span>
        </div>
      </div>

      <!-- 段位变化 -->
      <div class="tier-section" v-if="hasTierChange">
        <div class="tier-header">
          <span class="tier-label">段位变化</span>
          <span class="tier-change-badge" :class="isTierUpgrade ? 'upgrade' : 'downgrade'">
            {{ isTierUpgrade ? '晋升' : '降级' }}
          </span>
        </div>
        <div class="tier-detail">
          <TierBadge
            :tier="settlement.tier?.before"
            :name="settlement.tier?.name_before"
            size="medium"
          />
          <span class="tier-arrow">→</span>
          <TierBadge
            :tier="settlement.tier?.after"
            :name="settlement.tier?.name_after"
            size="medium"
          />
        </div>
      </div>

      <!-- 当前段位（无变化时显示） -->
      <div class="tier-section" v-else>
        <div class="tier-header">
          <span class="tier-label">当前段位</span>
        </div>
        <div class="tier-detail single">
          <TierBadge
            :tier="settlement.tier?.after"
            :name="settlement.tier?.name_after"
            :rating="settlement.rating?.after"
            size="large"
            show-rating
          />
        </div>
      </div>

      <!-- 奖励信息 -->
      <div class="rewards-section" v-if="hasRewards">
        <div class="section-title">获得奖励</div>
        <div class="rewards-list">
          <div class="reward-item" v-if="(settlement.rewards?.gold || 0) > 0">
            <span class="reward-icon">🪙</span>
            <span class="reward-name">金币</span>
            <span class="reward-value">+{{ settlement.rewards?.gold }}</span>
          </div>
          <div class="reward-item" v-if="(settlement.rewards?.experience || 0) > 0">
            <span class="reward-icon">⭐</span>
            <span class="reward-name">经验</span>
            <span class="reward-value">+{{ settlement.rewards?.experience }}</span>
          </div>
          <div class="reward-item" v-if="(settlement.rewards?.honor || settlement.rewards?.honor_points || 0) > 0">
            <span class="reward-icon">🏅</span>
            <span class="reward-name">荣誉</span>
            <span class="reward-value">+{{ settlement.rewards?.honor || settlement.rewards?.honor_points }}</span>
          </div>
        </div>
      </div>

      <!-- 成就解锁 -->
      <div class="achievements-section" v-if="hasAchievements">
        <div class="section-title">成就解锁</div>
        <div class="achievements-list">
          <div
            class="achievement-item"
            v-for="(achievement, index) in achievementList"
            :key="index"
            :class="`rarity-${getAchievementRarity(achievement).toLowerCase()}`"
          >
            <span class="achievement-icon">{{ achievementRarityIcon(getAchievementRarity(achievement)) }}</span>
            <span class="achievement-name">{{ getAchievementName(achievement) }}</span>
            <span class="achievement-rarity">{{ getAchievementRarity(achievement) }}</span>
          </div>
        </div>
      </div>

      <!-- 连胜信息 -->
      <div class="streak-section" v-if="settlement.new_win_streak && settlement.new_win_streak > 0">
        <div class="streak-info">
          <span class="streak-icon">🔥</span>
          <span class="streak-text">连胜 {{ settlement.new_win_streak }} 局</span>
        </div>
      </div>

      <!-- 排行榜变化 -->
      <div class="leaderboard-section" v-if="hasLeaderboardChange">
        <div class="leaderboard-info">
          <span class="leaderboard-label">排行榜排名</span>
          <span class="leaderboard-change">
            #{{ settlement.leaderboard_rank_change!.old_rank }}
            →
            #{{ settlement.leaderboard_rank_change!.new_rank }}
          </span>
        </div>
      </div>
    </div>

    <template #footer>
      <el-button type="primary" @click="handleClose" size="large">
        确定
      </el-button>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
/**
 * 游戏结算弹窗组件
 *
 * 文档依据: game_platform_gameplay_design.md
 * - §7.3 GameEndEvent 结算事件
 * - §16.2.4 消息 8 game_settlement
 *
 * 功能：
 * - 显示对局结果（胜/负/平）
 * - 显示评分和段位变化
 * - 显示获得的奖励
 * - 显示解锁的成就
 * - 显示连胜和排行榜变化
 */
import { computed } from 'vue'
import { useGameStore } from '@/stores/game.store'
import TierBadge from '@/components/common/TierBadge.vue'
import type { GameSettlementData } from '@/types/gamedata.types'

const props = defineProps<{
  modelValue: boolean
}>()

const emit = defineEmits<{
  'update:modelValue': [value: boolean]
}>()

const gameStore = useGameStore()

/**
 * 弹窗可见性
 */
const visible = computed({
  get: () => props.modelValue,
  set: (value) => emit('update:modelValue', value),
})

/**
 * 结算数据
 */
const settlement = computed((): GameSettlementData | null => {
  console.log('[GameSettlementModal] Computed settlement:', gameStore.settlement)
  return gameStore.settlement
})

/**
 * 对局结果
 * 支持大小写两种格式：WIN/LOSS/DRAW 或 win/lose/draw
 */
const result = computed((): 'win' | 'lose' | 'draw' => {
  const r = settlement.value?.result?.toLowerCase()
  if (r === 'win') return 'win'
  if (r === 'loss' || r === 'lose') return 'lose'
  return 'draw'
})

/**
 * 弹窗标题
 */
const dialogTitle = computed((): string => {
  const titles = {
    win: '对局胜利',
    lose: '对局失败',
    draw: '对局平局',
  }
  return titles[result.value]
})

/**
 * 结果图标
 */
const resultIcon = computed((): string => {
  const icons = {
    win: '🏆',
    lose: '😢',
    draw: '🤝',
  }
  return icons[result.value]
})

/**
 * 结果文本
 */
const resultText = computed((): string => {
  const texts = {
    win: '恭喜获胜！',
    lose: '再接再厉！',
    draw: '势均力敌！',
  }
  return texts[result.value]
})

/**
 * 结果样式类
 */
const resultClass = computed((): string => {
  return `result-${result.value}`
})

/**
 * 评分变化文本
 */
const ratingChangeText = computed((): string => {
  const change = settlement.value?.rating?.change || 0
  if (change > 0) return `+${change}`
  if (change < 0) return `${change}`
  return '±0'
})

/**
 * 评分变化样式类
 */
const ratingChangeClass = computed((): string => {
  const change = settlement.value?.rating?.change || 0
  if (change > 0) return 'positive'
  if (change < 0) return 'negative'
  return 'neutral'
})

/**
 * 是否有段位变化
 */
const hasTierChange = computed((): boolean => {
  if (!settlement.value?.tier) return false
  return settlement.value.tier.before !== settlement.value.tier.after
})

/**
 * 是否为段位晋升
 */
const isTierUpgrade = computed((): boolean => {
  if (!settlement.value?.tier) return false
  const tierBefore = settlement.value.tier.before
  const tierAfter = settlement.value.tier.after
  if (tierBefore === undefined || tierAfter === undefined) return false
  return tierAfter > tierBefore
})

/**
 * 是否有奖励
 */
const hasRewards = computed((): boolean => {
  if (!settlement.value?.rewards) return false
  const { gold, experience, honor, honor_points } = settlement.value.rewards
  return (gold || 0) > 0 || (experience || 0) > 0 || (honor || honor_points || 0) > 0
})

/**
 * 是否有成就解锁
 */
const hasAchievements = computed((): boolean => {
  return (settlement.value?.achievements_unlocked?.length || 0) > 0
})

/**
 * 成就列表（处理字符串和对象两种格式）
 */
const achievementList = computed(() => {
  const unlocked = settlement.value?.achievements_unlocked || []
  return unlocked.map(a => typeof a === 'string' ? { name: a, rarity: 'COMMON' } : a)
})

/**
 * 获取成就名称
 */
const getAchievementName = (achievement: any): string => {
  return achievement.name || achievement || '未知成就'
}

/**
 * 获取成就稀有度
 */
const getAchievementRarity = (achievement: any): string => {
  return achievement.rarity || 'COMMON'
}

/**
 * 是否有排行榜变化
 */
const hasLeaderboardChange = computed((): boolean => {
  return settlement.value?.leaderboard_rank_change !== undefined
})

/**
 * 成就稀有度图标
 */
const achievementRarityIcon = (rarity: string): string => {
  const icons: Record<string, string> = {
    COMMON: '⬜',
    RARE: '🔵',
    EPIC: '🟣',
    LEGENDARY: '🟡',
  }
  return icons[rarity] || '⬜'
}

/**
 * 关闭弹窗
 */
const handleClose = () => {
  gameStore.closeSettlementModal()
  visible.value = false
}
</script>

<style scoped>
.settlement-modal :deep(.el-dialog__header) {
  text-align: center;
  padding-bottom: 0;
}

.settlement-modal :deep(.el-dialog__body) {
  padding: 16px 24px;
}

.settlement-modal :deep(.el-dialog__footer) {
  text-align: center;
  padding-top: 8px;
}

.settlement-content {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

/* 结果展示 */
.result-section {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 20px;
  border-radius: 12px;
  background: linear-gradient(135deg, #f5f7fa 0%, #e4e7eb 100%);
}

.result-section.result-win {
  background: linear-gradient(135deg, #d4edda 0%, #c3e6cb 100%);
}

.result-section.result-lose {
  background: linear-gradient(135deg, #f8d7da 0%, #f5c6cb 100%);
}

.result-section.result-draw {
  background: linear-gradient(135deg, #fff3cd 0%, #ffeeba 100%);
}

.result-icon {
  font-size: 48px;
  margin-bottom: 8px;
}

.result-text {
  font-size: 20px;
  font-weight: 600;
  color: #333;
}

/* 评分区域 */
.rating-section {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 16px;
  background: #f8f9fa;
  border-radius: 8px;
}

.rating-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.rating-label {
  font-size: 14px;
  color: #666;
}

.rating-change {
  font-size: 18px;
  font-weight: 600;
}

.rating-change.positive {
  color: #28a745;
}

.rating-change.negative {
  color: #dc3545;
}

.rating-change.neutral {
  color: #6c757d;
}

.rating-detail {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  font-size: 24px;
}

.rating-before,
.rating-after {
  font-weight: 600;
}

.rating-arrow {
  color: #999;
  font-size: 20px;
}

/* 段位区域 */
.tier-section {
  padding: 16px;
  background: #f8f9fa;
  border-radius: 8px;
}

.tier-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.tier-label {
  font-size: 14px;
  color: #666;
}

.tier-change-badge {
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 12px;
  font-weight: 600;
}

.tier-change-badge.upgrade {
  background: #d4edda;
  color: #155724;
}

.tier-change-badge.downgrade {
  background: #f8d7da;
  color: #721c24;
}

.tier-detail {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 16px;
}

.tier-detail.single {
  gap: 0;
}

.tier-arrow {
  font-size: 24px;
  color: #999;
}

/* 奖励区域 */
.rewards-section,
.achievements-section {
  padding: 16px;
  background: #f8f9fa;
  border-radius: 8px;
}

.section-title {
  font-size: 14px;
  color: #666;
  margin-bottom: 12px;
}

.rewards-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.reward-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  background: white;
  border-radius: 6px;
}

.reward-icon {
  font-size: 20px;
}

.reward-name {
  flex: 1;
  font-size: 14px;
  color: #333;
}

.reward-value {
  font-size: 16px;
  font-weight: 600;
  color: #28a745;
}

/* 成就区域 */
.achievements-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.achievement-item {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  background: white;
  border-radius: 6px;
  border-left: 3px solid #ccc;
}

.achievement-item.rarity-common {
  border-left-color: #6c757d;
}

.achievement-item.rarity-rare {
  border-left-color: #007bff;
}

.achievement-item.rarity-epic {
  border-left-color: #6f42c1;
}

.achievement-item.rarity-legendary {
  border-left-color: #ffc107;
}

.achievement-icon {
  font-size: 20px;
}

.achievement-name {
  flex: 1;
  font-size: 14px;
  color: #333;
}

.achievement-rarity {
  font-size: 10px;
  padding: 2px 6px;
  border-radius: 4px;
  background: #e9ecef;
  color: #666;
}

/* 连胜区域 */
.streak-section {
  display: flex;
  justify-content: center;
}

.streak-info {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  background: linear-gradient(135deg, #ff9a56 0%, #ff6b6b 100%);
  border-radius: 20px;
  color: white;
}

.streak-icon {
  font-size: 20px;
}

.streak-text {
  font-size: 14px;
  font-weight: 600;
}

/* 排行榜变化 */
.leaderboard-section {
  padding: 12px;
  background: #e7f1ff;
  border-radius: 8px;
}

.leaderboard-info {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.leaderboard-label {
  font-size: 14px;
  color: #666;
}

.leaderboard-change {
  font-size: 14px;
  font-weight: 600;
  color: #0056b3;
}
</style>
