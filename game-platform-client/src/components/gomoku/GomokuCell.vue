<template>
  <div
    class="gomoku-cell"
    :class="{ 'has-piece': color, 'last-move': isLastMove, disabled }"
    @click="handleClick"
  >
    <!-- 网格线：垂直线 -->
    <div
      class="grid-line vertical"
      :class="{
        'edge-top': isEdgeTop,
        'edge-bottom': isEdgeBottom
      }"
    ></div>
    <!-- 网格线：水平线 -->
    <div
      class="grid-line horizontal"
      :class="{
        'edge-left': isEdgeLeft,
        'edge-right': isEdgeRight
      }"
    ></div>
    <!-- 星位标记 -->
    <div v-if="isStarPoint" class="star-point"></div>
    <!-- 棋子 -->
    <div v-if="color" class="piece" :class="color"></div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { Position, PlayerColor } from '@/types'

const props = defineProps<{
  x: number
  y: number
  color: PlayerColor | null
  lastMove: Position | null
  disabled?: boolean
  isStarPoint?: boolean
  isEdgeTop?: boolean
  isEdgeBottom?: boolean
  isEdgeLeft?: boolean
  isEdgeRight?: boolean
}>()

const emit = defineEmits<{
  move: [position: Position]
}>()

const isLastMove = computed(() => {
  return props.lastMove?.x === props.x && props.lastMove?.y === props.y
})

const handleClick = () => {
  if (props.disabled || props.color) return
  emit('move', { x: props.x, y: props.y })
}
</script>

<style scoped>
.gomoku-cell {
  position: relative;
  width: 30px;
  height: 30px;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
}

.gomoku-cell:hover:not(.has-piece):not(.disabled) {
  background: rgba(0, 0, 0, 0.05);
}

.grid-line {
  position: absolute;
  background: #8b7355;
}

/* 垂直线：默认从中心向两边延伸 */
.grid-line.vertical {
  width: 1px;
  height: 100%;
  left: 50%;
  transform: translateX(-50%);
}

/* 边缘单元格的垂直线处理 */
.grid-line.vertical.edge-top {
  /* 顶边：线条从中心向下延伸 */
  height: 50%;
  top: 50%;
}

.grid-line.vertical.edge-bottom {
  /* 底边：线条从中心向上延伸 */
  height: 50%;
  bottom: 50%;
  top: auto;
}

/* 水平线：默认从中心向两边延伸 */
.grid-line.horizontal {
  height: 1px;
  width: 100%;
  top: 50%;
  transform: translateY(-50%);
}

/* 边缘单元格的水平线处理 */
.grid-line.horizontal.edge-left {
  /* 左边：线条从中心向右延伸 */
  width: 50%;
  left: 50%;
}

.grid-line.horizontal.edge-right {
  /* 右边：线条从中心向左延伸 */
  width: 50%;
  right: 50%;
  left: auto;
}

/* 星位标记 */
.star-point {
  position: absolute;
  width: 8px;
  height: 8px;
  background: #8b7355;
  border-radius: 50%;
  z-index: 0;
}

.piece {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  position: relative;
  z-index: 1;
  box-shadow: 2px 2px 4px rgba(0, 0, 0, 0.4);
}

.piece.black {
  background: radial-gradient(circle at 30% 30%, #555, #000);
}

.piece.white {
  background: radial-gradient(circle at 30% 30%, #fff, #ddd);
}

.last-move::after {
  content: '';
  position: absolute;
  width: 6px;
  height: 6px;
  background: #f44336;
  border-radius: 50%;
  z-index: 2;
}

.disabled {
  cursor: not-allowed;
}
</style>
