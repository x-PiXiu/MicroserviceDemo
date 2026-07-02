<template>
  <div class="gomoku-history">
    <h3>棋谱</h3>
    <div class="history-list" v-if="moves.length > 0">
      <div
        v-for="(move, index) in moves"
        :key="index"
        class="history-item"
        :class="{ current: index === currentMoveIndex }"
      >
        <span class="move-number">{{ index + 1 }}.</span>
        <span class="move-color" :class="move.color">{{ move.color === 'black' ? '黑' : '白' }}</span>
        <span class="move-position">({{ move.position.x }}, {{ move.position.y }})</span>
      </div>
    </div>
    <div v-else class="empty">暂无棋谱</div>
  </div>
</template>

<script setup lang="ts">
import type { Move } from '@/types'

defineProps<{
  moves: Move[]
  currentMoveIndex: number
}>()

</script>

<style scoped>
.gomoku-history {
  background: var(--el-bg-color);
  border-radius: 8px;
  padding: 1rem;
  max-height: 400px;
  overflow-y: auto;
}

.gomoku-history h3 {
  margin: 0 0 1rem 0;
  font-size: 1rem;
}

.history-list {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.history-item {
  display: flex;
  gap: 0.5rem;
  padding: 0.5rem;
  border-radius: 4px;
  cursor: pointer;
  transition: background 0.2s;
}

.history-item:hover {
  background: var(--el-fill-color-light);
}

.history-item.current {
  background: var(--el-color-primary-light-9);
  border: 1px solid var(--el-color-primary);
}

.move-number {
  font-weight: bold;
  min-width: 30px;
}

.move-color {
  font-weight: bold;
  min-width: 20px;
}

.move-color.black {
  color: #333;
}

.move-color.white {
  color: #999;
}

.move-position {
  color: var(--el-text-color-secondary);
}

.empty {
  text-align: center;
  color: var(--el-text-color-secondary);
  padding: 1rem;
}
</style>
