<template>
  <div class="gomoku-timer">
    <div class="timer-display">
      <div class="timer-item" :class="{ active: currentTurn === 'black' }">
        <span class="label">{{ t('game.black') }}</span>
        <span class="time">{{ formatTime(blackTime) }}</span>
      </div>
      <div class="timer-item" :class="{ active: currentTurn === 'white' }">
        <span class="label">{{ t('game.white') }}</span>
        <span class="time">{{ formatTime(whiteTime) }}</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n'
import type { PlayerColor } from '@/types'

const { t } = useI18n()

defineProps<{
  currentTurn: PlayerColor
  blackTime: number
  whiteTime: number
}>()

const formatTime = (seconds: number) => {
  const mins = Math.floor(seconds / 60)
  const secs = seconds % 60
  return `${String(mins).padStart(2, '0')}:${String(secs).padStart(2, '0')}`
}
</script>

<style scoped>
.gomoku-timer {
  background: var(--el-bg-color);
  border-radius: 8px;
  padding: 1rem;
  margin-bottom: 1rem;
}

.timer-display {
  display: flex;
  justify-content: space-around;
  gap: 2rem;
}

.timer-item {
  text-align: center;
  padding: 1rem;
  border-radius: 8px;
  background: var(--el-fill-color-blank);
  transition: all 0.3s;
}

.timer-item.active {
  background: var(--el-color-primary-light-9);
  box-shadow: 0 0 10px var(--el-color-primary);
}

.label {
  display: block;
  font-size: 0.875rem;
  color: var(--el-text-color-secondary);
  margin-bottom: 0.5rem;
}

.time {
  display: block;
  font-size: 2rem;
  font-weight: bold;
  font-family: monospace;
}
</style>
