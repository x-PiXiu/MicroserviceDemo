<template>
  <Teleport to="body">
    <div class="achievement-notification-container">
      <TransitionGroup name="notification">
        <div
          v-for="(notification, index) in visibleNotifications"
          :key="notification.timestamp"
          class="achievement-notification"
          :class="`rarity-${getRarityClass(notification.rarity)}`"
          @click="handleDismiss(index)"
        >
          <div class="notification-glow"></div>
          <div class="notification-content">
            <div class="notification-header">
              <el-icon class="unlock-icon"><Unlock /></el-icon>
              <span class="header-text">成就解锁!</span>
            </div>

            <div class="notification-body">
              <div class="achievement-icon">
                <el-icon :size="32"><Trophy /></el-icon>
              </div>
              <div class="achievement-info">
                <div class="achievement-name">{{ notification.achievement_name }}</div>
                <div class="achievement-rarity" :class="`rarity-${getRarityClass(notification.rarity)}`">
                  {{ getRarityText(notification.rarity) }}
                </div>
                <div v-if="notification.points" class="achievement-points">
                  +{{ notification.points }} 积分
                </div>
              </div>
            </div>

            <div v-if="notification.reward" class="notification-rewards">
              <div v-if="notification.reward.coins" class="reward-item">
                <el-icon><Coin /></el-icon>
                <span>+{{ notification.reward.coins }}</span>
              </div>
              <div v-if="notification.reward.gems" class="reward-item">
                <el-icon><Present /></el-icon>
                <span>+{{ notification.reward.gems }}</span>
              </div>
              <div v-if="notification.reward.experience" class="reward-item">
                <el-icon><StarFilled /></el-icon>
                <span>+{{ notification.reward.experience }} EXP</span>
              </div>
            </div>
          </div>

          <div class="notification-progress" :style="{ width: `${progressMap[notification.timestamp!] || 100}%` }"></div>
        </div>
      </TransitionGroup>
    </div>
  </Teleport>
</template>

<script setup lang="ts">
import { ref, computed, watch, onUnmounted } from 'vue'
import { Unlock, Trophy, Coin, Present, StarFilled } from '@element-plus/icons-vue'
import { useGameStore, type AchievementNotification } from '@/stores/game.store'

const props = withDefaults(
  defineProps<{
    /** 自动关闭时间（毫秒） */
    duration?: number
    /** 最大同时显示数量 */
    maxVisible?: number
  }>(),
  {
    duration: 5000,
    maxVisible: 3,
  }
)

const emit = defineEmits<{
  (e: 'dismiss', notification: AchievementNotification): void
}>()

const gameStore = useGameStore()

// 进度条映射
const progressMap = ref<Record<number, number>>({})

// 定时器映射
const timerMap = ref<Map<number, ReturnType<typeof setInterval>>>(new Map())

// 可见的通知列表
const visibleNotifications = computed(() => {
  return gameStore.achievementNotifications.slice(0, props.maxVisible)
})

// 稀有度映射
const getRarityClass = (rarity?: string): string => {
  if (!rarity) return 'common'
  return rarity.toLowerCase()
}

const getRarityText = (rarity?: string): string => {
  const rarityMap: Record<string, string> = {
    COMMON: '普通',
    RARE: '稀有',
    EPIC: '史诗',
    LEGENDARY: '传说',
    common: '普通',
    rare: '稀有',
    epic: '史诗',
    legendary: '传说',
  }
  return rarityMap[rarity || ''] || rarity || '普通'
}

// 监听新通知
watch(
  () => gameStore.achievementNotifications.length,
  (newLength, oldLength) => {
    if (newLength > (oldLength || 0)) {
      // 有新通知
      const newNotification = gameStore.achievementNotifications[newLength - 1]
      if (newNotification) {
        startProgress(newNotification.timestamp!)
      }
    }
  }
)

// 开始进度条倒计时
const startProgress = (timestamp: number) => {
  // 清除已存在的定时器
  const existingTimer = timerMap.value.get(timestamp)
  if (existingTimer) {
    clearInterval(existingTimer)
  }

  progressMap.value[timestamp] = 100

  const startTime = Date.now()
  const interval = 50 // 更新间隔

  const timer = setInterval(() => {
    const elapsed = Date.now() - startTime
    const remaining = Math.max(0, 100 - (elapsed / props.duration) * 100)

    progressMap.value[timestamp] = remaining

    if (remaining <= 0) {
      clearInterval(timer)
      timerMap.value.delete(timestamp)
      dismissByTimestamp(timestamp)
    }
  }, interval)

  timerMap.value.set(timestamp, timer)
}

// 通过时间戳关闭通知
const dismissByTimestamp = (timestamp: number) => {
  const index = gameStore.achievementNotifications.findIndex((n) => n.timestamp === timestamp)
  if (index >= 0) {
    const notification = gameStore.achievementNotifications[index]
    gameStore.removeAchievementNotification(index)
    emit('dismiss', notification)
  }
}

// 手动关闭
const handleDismiss = (index: number) => {
  const notification = visibleNotifications.value[index]
  if (notification && notification.timestamp) {
    // 清除定时器
    const timer = timerMap.value.get(notification.timestamp)
    if (timer) {
      clearInterval(timer)
      timerMap.value.delete(notification.timestamp)
    }
  }
  gameStore.removeAchievementNotification(index)
  if (notification) {
    emit('dismiss', notification)
  }
}

// 清理
onUnmounted(() => {
  timerMap.value.forEach((timer) => clearInterval(timer))
  timerMap.value.clear()
})
</script>

<style scoped>
.achievement-notification-container {
  position: fixed;
  top: 80px;
  right: 20px;
  z-index: 9999;
  display: flex;
  flex-direction: column;
  gap: 12px;
  pointer-events: none;
}

.achievement-notification {
  position: relative;
  width: 320px;
  padding: 16px;
  border-radius: 12px;
  background: linear-gradient(135deg, var(--el-bg-color) 0%, var(--el-fill-color-blank) 100%);
  border: 2px solid;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.2);
  pointer-events: auto;
  cursor: pointer;
  overflow: hidden;
}

.achievement-notification:hover {
  transform: translateX(-5px);
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.25);
}

/* 稀有度边框颜色 */
.achievement-notification.rarity-common {
  border-color: #909399;
}

.achievement-notification.rarity-rare {
  border-color: #409eff;
}

.achievement-notification.rarity-epic {
  border-color: #a855f7;
}

.achievement-notification.rarity-legendary {
  border-color: #f59e0b;
  animation: legendaryGlow 2s ease-in-out infinite;
}

@keyframes legendaryGlow {
  0%,
  100% {
    box-shadow: 0 8px 32px rgba(245, 158, 11, 0.3);
  }
  50% {
    box-shadow: 0 8px 48px rgba(245, 158, 11, 0.5);
  }
}

/* 光晕效果 */
.notification-glow {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  background: linear-gradient(90deg, transparent, var(--el-color-primary), transparent);
  animation: glowSlide 2s ease-in-out infinite;
}

@keyframes glowSlide {
  0% {
    transform: translateX(-100%);
  }
  100% {
    transform: translateX(100%);
  }
}

.notification-content {
  position: relative;
  z-index: 1;
}

.notification-header {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 12px;
  padding-bottom: 8px;
  border-bottom: 1px solid var(--el-border-color-lighter);
}

.unlock-icon {
  color: #67c23a;
  animation: bounce 0.6s ease-in-out;
}

@keyframes bounce {
  0%,
  100% {
    transform: scale(1);
  }
  50% {
    transform: scale(1.2);
  }
}

.header-text {
  font-size: 0.875rem;
  font-weight: 600;
  color: #67c23a;
  text-transform: uppercase;
  letter-spacing: 1px;
}

.notification-body {
  display: flex;
  align-items: center;
  gap: 16px;
  margin-bottom: 12px;
}

.achievement-icon {
  width: 56px;
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 50%;
  background: linear-gradient(135deg, #ffd700 0%, #ffb700 100%);
  color: #fff;
  flex-shrink: 0;
}

.achievement-info {
  flex: 1;
  min-width: 0;
}

.achievement-name {
  font-size: 1rem;
  font-weight: 600;
  color: var(--el-text-color-primary);
  margin-bottom: 4px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.achievement-rarity {
  font-size: 0.75rem;
  font-weight: 500;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin-bottom: 4px;
}

.achievement-rarity.rarity-common {
  color: #909399;
}

.achievement-rarity.rarity-rare {
  color: #409eff;
}

.achievement-rarity.rarity-epic {
  color: #a855f7;
}

.achievement-rarity.rarity-legendary {
  color: #f59e0b;
}

.achievement-points {
  font-size: 0.75rem;
  color: var(--el-text-color-secondary);
}

.notification-rewards {
  display: flex;
  gap: 12px;
  padding-top: 8px;
  border-top: 1px solid var(--el-border-color-lighter);
}

.reward-item {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 0.8rem;
  color: var(--el-text-color-regular);
}

.reward-item .el-icon {
  font-size: 14px;
  color: #ffd700;
}

.notification-progress {
  position: absolute;
  bottom: 0;
  left: 0;
  height: 3px;
  background: linear-gradient(90deg, var(--el-color-primary), var(--el-color-primary-light-3));
  transition: width 0.05s linear;
}

/* 过渡动画 */
.notification-enter-active {
  animation: slideIn 0.4s ease-out;
}

.notification-leave-active {
  animation: slideOut 0.3s ease-in;
}

@keyframes slideIn {
  from {
    opacity: 0;
    transform: translateX(100%);
  }
  to {
    opacity: 1;
    transform: translateX(0);
  }
}

@keyframes slideOut {
  from {
    opacity: 1;
    transform: translateX(0);
  }
  to {
    opacity: 0;
    transform: translateX(100%);
  }
}
</style>
