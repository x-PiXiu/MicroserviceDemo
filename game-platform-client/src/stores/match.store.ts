/**
 * 匹配系统状态管理
 *
 * 文档依据: game_platform_gameplay_design.md
 * - §6 匹配系统
 * - §16.2.4 匹配消息类型
 *
 * 功能：
 * - 管理匹配状态（搜索中、匹配成功、超时等）
 * - 存储匹配结果和对手信息
 * - 提供匹配相关的 actions
 */
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

/** 匹配状态类型 */
export type MatchStatus = 'idle' | 'searching' | 'matched' | 'timeout' | 'cancelled'

/** 匹配结果数据 */
export interface MatchResultData {
  match_id: string
  room_id: string
  opponent: {
    user_id: string
    username: string
    rating: number
    tier: number
    tier_name: string
  }
  your_piece: 1 | 2
  config: {
    game_mode: string
    time_limit: number
    increment_per_move: number
  }
}

/** 匹配状态数据 */
export interface MatchStatusData {
  status: MatchStatus
  queue_size?: number
  estimated_wait_time?: number
  match_id?: string
}

export const useMatchStore = defineStore('match', () => {
  // ==================== State ====================

  /** 当前匹配状态 */
  const matchStatus = ref<MatchStatus>('idle')

  /** 队列大小 */
  const queueSize = ref<number>(0)

  /** 预计等待时间（秒） */
  const estimatedWaitTime = ref<number>(0)

  /** 匹配开始时间 */
  const matchStartTime = ref<number | null>(null)

  /** 匹配结果 */
  const matchResult = ref<MatchResultData | null>(null)

  /** 匹配 ID */
  const matchId = ref<string | null>(null)

  /** 超时原因 */
  const timeoutReason = ref<string | null>(null)

  /** 是否可以重试 */
  const canRetry = ref<boolean>(true)

  // ==================== Getters ====================

  /** 是否正在匹配中 */
  const isMatching = computed(() => matchStatus.value === 'searching')

  /** 是否匹配成功 */
  const isMatched = computed(() => matchStatus.value === 'matched')

  /** 是否超时 */
  const isTimeout = computed(() => matchStatus.value === 'timeout')

  /** 是否取消 */
  const isCancelled = computed(() => matchStatus.value === 'cancelled')

  /** 实际等待时间（秒） */
  const actualWaitTime = computed(() => {
    if (!matchStartTime.value) return 0
    return Math.floor((Date.now() - matchStartTime.value) / 1000)
  })

  /** 格式化等待时间 */
  const formattedWaitTime = computed(() => {
    const seconds = actualWaitTime.value
    const mins = Math.floor(seconds / 60)
    const secs = seconds % 60
    return `${mins}:${secs.toString().padStart(2, '0')}`
  })

  // ==================== Actions ====================

  /**
   * 开始匹配
   */
  const startMatching = () => {
    matchStatus.value = 'searching'
    matchStartTime.value = Date.now()
    matchResult.value = null
    timeoutReason.value = null
    canRetry.value = true
    console.log('[MatchStore] Matching started')
  }

  /**
   * 更新匹配状态
   */
  const updateMatchStatus = (data: MatchStatusData) => {
    matchStatus.value = data.status
    queueSize.value = data.queue_size ?? queueSize.value
    estimatedWaitTime.value = data.estimated_wait_time ?? estimatedWaitTime.value
    if (data.match_id) {
      matchId.value = data.match_id
    }
    console.log('[MatchStore] Status updated:', data)
  }

  /**
   * 设置匹配成功
   */
  const setMatchFound = (data: MatchResultData) => {
    matchStatus.value = 'matched'
    matchResult.value = data
    matchId.value = data.match_id
    console.log('[MatchStore] Match found:', data)
  }

  /**
   * 设置匹配超时
   */
  const setMatchTimeout = (reason: string, waitTime: number, retry: boolean) => {
    matchStatus.value = 'timeout'
    timeoutReason.value = reason
    canRetry.value = retry
    console.log('[MatchStore] Match timeout:', reason, 'wait time:', waitTime)
  }

  /**
   * 取消匹配
   */
  const cancelMatching = () => {
    matchStatus.value = 'cancelled'
    matchStartTime.value = null
    console.log('[MatchStore] Match cancelled')
  }

  /**
   * 重置匹配状态
   */
  const resetMatch = () => {
    matchStatus.value = 'idle'
    queueSize.value = 0
    estimatedWaitTime.value = 0
    matchStartTime.value = null
    matchResult.value = null
    matchId.value = null
    timeoutReason.value = null
    canRetry.value = true
    console.log('[MatchStore] Match reset')
  }

  /**
   * 清除匹配结果（进入房间后调用）
   */
  const clearMatchResult = () => {
    matchResult.value = null
    matchStatus.value = 'idle'
    matchStartTime.value = null
  }

  return {
    // State
    matchStatus,
    queueSize,
    estimatedWaitTime,
    matchStartTime,
    matchResult,
    matchId,
    timeoutReason,
    canRetry,

    // Getters
    isMatching,
    isMatched,
    isTimeout,
    isCancelled,
    actualWaitTime,
    formattedWaitTime,

    // Actions
    startMatching,
    updateMatchStatus,
    setMatchFound,
    setMatchTimeout,
    cancelMatching,
    resetMatch,
    clearMatchResult,
  }
})
