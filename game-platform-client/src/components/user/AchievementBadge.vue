<template>
  <div class="achievement-badge" :class="[`rarity-${achievement.rarity}`, { unlocked: achievement.isUnlocked }]" @click="handleClick">
    <div class="badge-icon">
      <el-image v-if="achievement.isUnlocked" :src="achievement.icon" fit="cover">
        <template #error>
          <el-icon :size="32"><Trophy /></el-icon>
        </template>
      </el-image>
      <el-icon v-else :size="32" class="locked"><Lock /></el-icon>
    </div>
    <div class="badge-info">
      <div class="badge-name">{{ achievement.name }}</div>
      <div class="badge-rarity">{{ getRarityText(achievement.rarity) }}</div>
    </div>
    <el-icon v-if="achievement.isUnlocked" class="badge-check" color="#67c23a"><CircleCheck /></el-icon>
  </div>
</template>

<script setup lang="ts">
import { Trophy, Lock, CircleCheck } from '@element-plus/icons-vue'
import type { Achievement } from '@/types'

const props = defineProps<{
  achievement: Achievement
}>()

const emit = defineEmits<{
  (e: 'click', achievement: Achievement): void
}>()

const getRarityText = (rarity: string) => {
  const rarityMap: Record<string, string> = {
    common: '普通',
    rare: '稀有',
    epic: '史诗',
    legendary: '传说',
  }
  return rarityMap[rarity] || rarity
}

const handleClick = (e: Event) => {
  e.stopPropagation()
  emit('click', props.achievement)
}
</script>

<style scoped>
.achievement-badge {
  position: relative;
  display: inline-flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.75rem 1rem;
  border-radius: 8px;
  background: var(--el-fill-color-blank);
  border: 2px solid;
  transition: all 0.3s;
}

.achievement-badge.unlocked {
  cursor: pointer;
}

.achievement-badge.unlocked:hover {
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.achievement-badge:not(.unlocked) {
  opacity: 0.6;
  filter: grayscale(100%);
}

.achievement-badge.rarity-common {
  border-color: #909399;
}

.achievement-badge.rarity-rare {
  border-color: #409eff;
}

.achievement-badge.rarity-epic {
  border-color: #a855f7;
}

.achievement-badge.rarity-legendary {
  border-color: #f59e0b;
}

.badge-icon {
  width: 48px;
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 50%;
  background: var(--el-fill-color-light);
}

.badge-icon :deep(.el-image) {
  width: 100%;
  height: 100%;
  border-radius: 50%;
}

.badge-icon .locked {
  color: var(--el-text-color-placeholder);
}

.badge-info {
  flex: 1;
}

.badge-name {
  font-weight: 500;
  font-size: 0.875rem;
  margin-bottom: 0.25rem;
}

.badge-rarity {
  font-size: 0.75rem;
  color: var(--el-text-color-secondary);
}

.badge-check {
  position: absolute;
  top: -8px;
  right: -8px;
  font-size: 20px;
  background: var(--el-bg-color);
  border-radius: 50%;
  padding: 2px;
}
</style>
