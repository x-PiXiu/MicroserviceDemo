<template>
  <div class="gomoku-board">
    <div class="board" :style="{ width: boardSize + 'px', height: boardSize + 'px' }">
      <div
        v-for="(row, x) in board"
        :key="x"
        class="row"
      >
        <GomokuCell
          v-for="(cell, y) in row"
          :key="`${x}-${y}`"
          :x="x"
          :y="y"
          :color="cell"
          :last-move="lastMovePosition"
          :disabled="disabled"
          :is-star-point="isStarPoint(x, y)"
          :is-edge-top="x === 0"
          :is-edge-bottom="x === board.length - 1"
          :is-edge-left="y === 0"
          :is-edge-right="y === row.length - 1"
          @move="handleMove"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import GomokuCell from './GomokuCell.vue'
import type { Position, PlayerColor } from '@/types'

const props = defineProps<{
  board: (PlayerColor | null)[][]
  currentPlayer: PlayerColor
  disabled?: boolean
  lastMove?: { row: number; col: number } | null
}>()

const emit = defineEmits<{
  move: [position: Position]
}>()

const cellSize = 30
const boardSize = computed(() => (props.board.length - 1) * cellSize + cellSize)

// v2.4.3: 判断是否是星位
// 标准15x15五子棋棋盘的星位（共9个）：
// - 4个角星：距离角3格的位置
// - 4个边星：各边中间位置
// - 1个天元：正中心
// 坐标从0开始，所以第4条线是索引3，第8条线是索引7，第12条线是索引11
const STAR_POINTS_15: [number, number][] = [
  [3, 3],   // 左上角星
  [3, 11],  // 右上角星
  [11, 3],  // 左下角星
  [11, 11], // 右下角星
  [3, 7],   // 上边星
  [7, 3],   // 左边星
  [7, 11],  // 右边星
  [11, 7],  // 下边星
  [7, 7],   // 天元（中心点）
]

const isStarPoint = (x: number, y: number): boolean => {
  // 只对15x15棋盘显示星位
  if (props.board.length !== 15) return false
  const result = STAR_POINTS_15.some(([sx, sy]) => sx === x && sy === y)
  // 调试：只打印匹配的星位
  if (result) {
    console.log(`[GomokuBoard] Star point at (${x}, ${y})`)
  }
  return result
}

const lastMovePosition = computed<Position | null>(() => {
  if (!props.lastMove) return null
  return { x: props.lastMove.row, y: props.lastMove.col }
})

const handleMove = (position: Position) => {
  if (props.disabled || props.board[position.x][position.y] !== null) return
  emit('move', position)
}
</script>

<style scoped>
.gomoku-board {
  display: flex;
  justify-content: center;
  align-items: center;
  padding: 1rem;
}

.board {
  position: relative;
  background: #e8c896;
  border-radius: 8px;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
}

.row {
  display: flex;
}

@media (max-width: 768px) {
  .board {
    transform: scale(0.8);
  }
}
</style>
