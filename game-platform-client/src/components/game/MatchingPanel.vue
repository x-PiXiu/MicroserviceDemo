<template>
  <el-dialog
    v-model="visible"
    :title="dialogTitle"
    width="420px"
    :close-on-click-modal="false"
    :close-on-press-escape="false"
    :show-close="isClosable"
    class="matching-panel"
    @close="handleClose"
  >
    <div class="matching-content">
      <!-- 搜索中状态 -->
      <div v-if="isMatching" class="matching-searching">
        <div class="search-animation">
          <div class="pulse-ring"></div>
          <div class="pulse-ring delay-1"></div>
          <div class="pulse-ring delay-2"></div>
          <div class="search-icon">
            <el-icon :size="48"><Search /></el-icon>
          </div>
        </div>
        <div class="search-status">
          <span class="status-text">正在寻找对手...</span>
          <span class="wait-time">{{ matchStore.formattedWaitTime }}</span>
        </div>
        <div class="queue-info" v-if="matchStore.queueSize > 0">
          <el-icon><User /></el-icon>
          <span>当前队列: {{ matchStore.queueSize }} 人</span>
        </div>
        <div class="estimated-time" v-if="matchStore.estimatedWaitTime > 0">
          <span>预计等待: ~{{ formatEstimatedTime(matchStore.estimatedWaitTime) }}</span>
        </div>
        <el-button type="danger" @click="handleCancel" :loading="cancelling">
          取消匹配
        </el-button>
      </div>

      <!-- 匹配成功状态 -->
      <div v-else-if="isMatched && matchResult" class="matching-found">
        <div class="success-icon">
          <el-icon :size="64" color="#10b981"><CircleCheckFilled /></el-icon>
        </div>
        <div class="success-text">匹配成功！</div>

        <div class="opponent-card">
          <div class="opponent-header">
            <span class="your-piece" :class="yourPieceClass">
              你执 {{ yourPieceText }}
            </span>
          </div>
          <div class="opponent-info">
            <el-avatar :size="48" class="opponent-avatar">
              {{ matchResult.opponent.username?.[0]?.toUpperCase() || '?' }}
            </el-avatar>
            <div class="opponent-details">
              <span class="opponent-name">{{ matchResult.opponent.username }}</span>
              <div class="opponent-rating">
                <el-icon><Trophy /></el-icon>
                <span>{{ matchResult.opponent.rating }}</span>
                <el-tag size="small" effect="plain">{{ matchResult.opponent.tier_name }}</el-tag>
              </div>
            </div>
          </div>
        </div>

        <div class="game-config">
          <div class="config-item">
            <span class="config-label">模式</span>
            <span class="config-value">{{ matchResult.config.game_mode }}</span>
          </div>
          <div class="config-item">
            <span class="config-label">时限</span>
            <span class="config-value">{{ formatTimeLimit(matchResult.config.time_limit) }}</span>
          </div>
        </div>

        <div class="countdown" v-if="countdown > 0">
          <span>{{ countdown }} 秒后自动进入房间</span>
        </div>
      </div>

      <!-- 匹配超时状态 -->
      <div v-else-if="isTimeout" class="matching-timeout">
        <div class="timeout-icon">
          <el-icon :size="64" color="#f59e0b"><WarningFilled /></el-icon>
        </div>
        <div class="timeout-text">匹配超时</div>
        <div class="timeout-reason" v-if="matchStore.timeoutReason">
          {{ matchStore.timeoutReason }}
        </div>
        <div class="timeout-actions">
          <el-button v-if="matchStore.canRetry" type="primary" @click="handleRetry">
            重新匹配
          </el-button>
          <el-button @click="handleClose">
            返回大厅
          </el-button>
        </div>
      </div>

      <!-- 取消状态 -->
      <div v-else-if="isCancelled" class="matching-cancelled">
        <div class="cancel-icon">
          <el-icon :size="64" color="#6b7280"><CircleCloseFilled /></el-icon>
        </div>
        <div class="cancel-text">已取消匹配</div>
        <el-button type="primary" @click="handleRetry">
          重新匹配
        </el-button>
      </div>
    </div>
  </el-dialog>
</template>

<script setup lang="ts">
/**
 * 匹配面板组件
 *
 * 文档依据: game_platform_gameplay_design.md
 * - §6 匹配系统
 * - §16.2.4 匹配消息类型
 *
 * 功能：
 * - 显示匹配状态（搜索中/匹配成功/超时/取消）
 * - 显示等待时间和队列信息
 * - 显示匹配成功后的对手信息
 * - 支持取消匹配和重新匹配
 */
import { ref, computed, watch, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import {
  Search,
  User,
  Trophy,
  CircleCheckFilled,
  WarningFilled,
  CircleCloseFilled,
} from '@element-plus/icons-vue'
import { useMatchStore } from '@/stores/match.store'
import { useGomokuSocket } from '@/websocket'

const props = defineProps<{
  modelValue: boolean
  gameType?: string
}>()

const emit = defineEmits<{
  'update:modelValue': [value: boolean]
  'matched': [roomId: string]
  'cancel': []
  'retry': []
}>()

const router = useRouter()
const matchStore = useMatchStore()
const { startMatch, cancelMatch } = useGomokuSocket()

// State
const cancelling = ref(false)
const countdown = ref(5)
let countdownTimer: ReturnType<typeof setInterval> | null = null

// Computed
const visible = computed({
  get: () => props.modelValue,
  set: (value) => emit('update:modelValue', value),
})

const isMatching = computed(() => matchStore.isMatching)
const isMatched = computed(() => matchStore.isMatched)
const isTimeout = computed(() => matchStore.isTimeout)
const isCancelled = computed(() => matchStore.isCancelled)

const matchResult = computed(() => matchStore.matchResult)

const isClosable = computed(() => !isMatching.value)

const dialogTitle = computed(() => {
  if (isMatching.value) return '快速匹配'
  if (isMatched.value) return '匹配成功'
  if (isTimeout.value) return '匹配超时'
  if (isCancelled.value) return '已取消'
  return '快速匹配'
})

const yourPieceClass = computed(() => {
  if (!matchResult.value) return ''
  return matchResult.value.your_piece === 1 ? 'black' : 'white'
})

const yourPieceText = computed(() => {
  if (!matchResult.value) return ''
  return matchResult.value.your_piece === 1 ? '黑方' : '白方'
})

// Methods
const formatEstimatedTime = (seconds: number): string => {
  if (seconds < 60) return `${seconds}秒`
  const mins = Math.floor(seconds / 60)
  const secs = seconds % 60
  return secs > 0 ? `${mins}分${secs}秒` : `${mins}分钟`
}

const formatTimeLimit = (seconds: number): string => {
  const mins = Math.floor(seconds / 60)
  return `${mins}分钟`
}

const handleCancel = async () => {
  cancelling.value = true
  try {
    cancelMatch()
    matchStore.cancelMatching()
    emit('cancel')
  } finally {
    cancelling.value = false
  }
}

const handleRetry = () => {
  startMatch(props.gameType || 'gomoku')
  matchStore.startMatching()
  emit('retry')
}

const handleClose = () => {
  if (isMatching.value) {
    handleCancel()
  }
  visible.value = false
}

const startCountdown = () => {
  countdown.value = 5
  if (countdownTimer) {
    clearInterval(countdownTimer)
  }
  countdownTimer = setInterval(() => {
    countdown.value--
    if (countdown.value <= 0) {
      clearInterval(countdownTimer!)
      countdownTimer = null
      // 自动跳转到房间
      if (matchResult.value) {
        router.push(`/game/${props.gameType || 'gomoku'}/room/${matchResult.value.room_id}`)
        matchStore.clearMatchResult()
        visible.value = false
      }
    }
  }, 1000)
}

// Watch for match result changes
watch(() => matchStore.matchResult, (result) => {
  if (result) {
    startCountdown()
    emit('matched', result.room_id)
  }
}, { immediate: true })

// Setup WebSocket listeners
onUnmounted(() => {
  if (countdownTimer) {
    clearInterval(countdownTimer)
  }
})
</script>

<style scoped>
.matching-panel :deep(.el-dialog__header) {
  text-align: center;
}

.matching-panel :deep(.el-dialog__body) {
  padding: 24px;
}

.matching-content {
  display: flex;
  flex-direction: column;
  align-items: center;
  min-height: 280px;
}

/* 搜索中状态 */
.matching-searching {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 20px;
}

.search-animation {
  position: relative;
  width: 100px;
  height: 100px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.pulse-ring {
  position: absolute;
  width: 100%;
  height: 100%;
  border-radius: 50%;
  border: 2px solid #667eea;
  animation: pulse 2s ease-out infinite;
}

.pulse-ring.delay-1 {
  animation-delay: 0.5s;
}

.pulse-ring.delay-2 {
  animation-delay: 1s;
}

@keyframes pulse {
  0% {
    transform: scale(0.8);
    opacity: 1;
  }
  100% {
    transform: scale(1.5);
    opacity: 0;
  }
}

.search-icon {
  width: 60px;
  height: 60px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  border-radius: 50%;
  color: white;
}

.search-status {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
}

.status-text {
  font-size: 16px;
  color: #374151;
  font-weight: 500;
}

.wait-time {
  font-size: 24px;
  font-weight: 700;
  color: #667eea;
  font-family: 'Courier New', monospace;
}

.queue-info,
.estimated-time {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 13px;
  color: #6b7280;
}

/* 匹配成功状态 */
.matching-found {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
  width: 100%;
}

.success-icon {
  animation: bounce 0.5s ease-out;
}

@keyframes bounce {
  0%, 100% { transform: scale(1); }
  50% { transform: scale(1.1); }
}

.success-text {
  font-size: 18px;
  font-weight: 600;
  color: #10b981;
}

.opponent-card {
  width: 100%;
  background: #f8fafc;
  border-radius: 12px;
  padding: 16px;
}

.opponent-header {
  margin-bottom: 12px;
}

.your-piece {
  padding: 4px 12px;
  border-radius: 4px;
  font-size: 13px;
  font-weight: 500;
}

.your-piece.black {
  background: #1a1a1a;
  color: white;
}

.your-piece.white {
  background: white;
  color: #1a1a1a;
  border: 1px solid #e5e7eb;
}

.opponent-info {
  display: flex;
  align-items: center;
  gap: 12px;
}

.opponent-avatar {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  font-size: 20px;
}

.opponent-details {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.opponent-name {
  font-size: 16px;
  font-weight: 600;
  color: #1a1a2e;
}

.opponent-rating {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 13px;
  color: #f59e0b;
}

.game-config {
  display: flex;
  justify-content: center;
  gap: 24px;
}

.config-item {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.config-label {
  font-size: 12px;
  color: #6b7280;
}

.config-value {
  font-size: 14px;
  font-weight: 500;
  color: #374151;
}

.countdown {
  font-size: 13px;
  color: #6b7280;
}

/* 超时状态 */
.matching-timeout {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
}

.timeout-text {
  font-size: 18px;
  font-weight: 600;
  color: #f59e0b;
}

.timeout-reason {
  font-size: 13px;
  color: #6b7280;
  text-align: center;
}

.timeout-actions {
  display: flex;
  gap: 12px;
}

/* 取消状态 */
.matching-cancelled {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
}

.cancel-text {
  font-size: 18px;
  font-weight: 600;
  color: #6b7280;
}
</style>
