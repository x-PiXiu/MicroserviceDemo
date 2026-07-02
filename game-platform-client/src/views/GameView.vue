<template>
  <div class="game-page">
    <!-- 游戏头部 -->
    <div class="game-header">
      <div class="room-info">
        <el-button text @click="handleLeave" class="back-btn">
          <el-icon><ArrowLeft /></el-icon>
          返回大厅
        </el-button>
        <div class="room-title">
          <h1>{{ currentRoom?.name || '游戏房间' }}</h1>
          <el-tag :type="gameStatusType" effect="dark" size="small">
            {{ gameStatusText }}
          </el-tag>
        </div>
      </div>
      <div class="header-actions">
        <el-button @click="showSettings = true" circle>
          <el-icon><Setting /></el-icon>
        </el-button>
        <el-button type="danger" @click="handleLeave">
          <el-icon><SwitchButton /></el-icon>
          离开房间
        </el-button>
      </div>
    </div>

    <!-- 游戏主内容 -->
    <div class="game-content">
      <!-- 左侧：玩家信息 -->
      <div class="side-panel left-panel">
        <div class="player-card" :class="{ active: currentPlayer === 'black', winner: winner === 'black' }">
          <div class="player-avatar black-piece">
            <span>黑</span>
          </div>
          <div class="player-details">
            <div class="player-name">
              <span :title="players.black?.username">{{ players.black?.username || '等待玩家...' }}</span>
              <el-tag v-if="players.black && isGameWaiting" :type="blackReadyState ? 'success' : 'info'" size="small" effect="plain">
                {{ blackReadyState ? '已准备' : '未准备' }}
              </el-tag>
            </div>
            <div class="player-stats">
              <span class="rating">
                <el-icon><TrophyBase /></el-icon>
                {{ players.black?.rating || '---' }}
              </span>
              <span class="captures">提子: {{ blackCaptures }}</span>
            </div>
          </div>
          <div class="time-display" :class="{ danger: blackTime < 60 }">
            {{ formatTime(blackTime) }}
          </div>
        </div>

        <!-- 游戏信息 -->
        <div class="game-info-card">
          <h3>游戏信息</h3>
          <div class="info-item">
            <span class="label">游戏模式</span>
            <span class="value">{{ gameConfig.gameModeStr || '自由模式' }}</span>
          </div>
          <div class="info-item">
            <span class="label">总步数</span>
            <span class="value">{{ totalMoves }}</span>
          </div>
          <div class="info-item">
            <span class="label">每步加时</span>
            <span class="value">+{{ gameConfig.incrementPerMove || 10 }}秒</span>
          </div>
        </div>

        <!-- 操作按钮 -->
        <div class="action-buttons">
          <!-- 准备按钮（等待/准备状态） -->
          <el-button
            v-if="isGameWaiting && players.black && players.white"
            :type="myReadyState ? 'success' : 'primary'"
            size="large"
            @click="handleReady"
            :loading="readying"
          >
            <el-icon><Check v-if="!myReadyState" /><CircleCheck v-else /></el-icon>
            {{ myReadyState ? '取消准备' : '准备' }}
          </el-button>
          <el-button
            v-if="canStartGame"
            type="success"
            size="large"
            @click="handleStartGame"
            :loading="starting"
          >
            <el-icon><VideoPlay /></el-icon>
            开始游戏
          </el-button>
          <el-button
            v-if="isGamePlaying && isMyTurn"
            @click="handleUndo"
            :disabled="!gameConfig.allowUndo"
          >
            <el-icon><RefreshLeft /></el-icon>
            悔棋
          </el-button>
          <el-button
            v-if="isGamePlaying"
            type="warning"
            @click="handleDrawOffer"
          >
            <el-icon><Connection /></el-icon>
            求和
          </el-button>
          <el-button
            v-if="isGamePlaying"
            type="danger"
            @click="handleSurrender"
          >
            <el-icon><Flag /></el-icon>
            认输
          </el-button>
        </div>
      </div>

      <!-- 中间：棋盘 -->
      <div class="board-container">
        <GomokuBoard
          :board="board"
          :currentPlayer="currentPlayer"
          :disabled="!canMove"
          :last-move="lastMove"
          @move="handleMove"
        />
        <div class="turn-indicator" v-if="isGamePlaying">
          <div class="turn-piece" :class="currentPlayer"></div>
          <span>{{ isMyTurn ? '轮到您下棋' : '等待对手...' }}</span>
        </div>
      </div>

      <!-- 右侧：对手和聊天 -->
      <div class="side-panel right-panel">
        <div class="player-card" :class="{ active: currentPlayer === 'white', winner: winner === 'white' }">
          <div class="player-avatar white-piece">
            <span>白</span>
          </div>
          <div class="player-details">
            <div class="player-name">
              <span :title="players.white?.username">{{ players.white?.username || '等待玩家...' }}</span>
              <el-tag v-if="players.white && isGameWaiting" :type="whiteReadyState ? 'success' : 'info'" size="small" effect="plain">
                {{ whiteReadyState ? '已准备' : '未准备' }}
              </el-tag>
            </div>
            <div class="player-stats">
              <span class="rating">
                <el-icon><TrophyBase /></el-icon>
                {{ players.white?.rating || '---' }}
              </span>
              <span class="captures">提子: {{ whiteCaptures }}</span>
            </div>
          </div>
          <div class="time-display" :class="{ danger: whiteTime < 60 }">
            {{ formatTime(whiteTime) }}
          </div>
        </div>

        <!-- 聊天区域 -->
        <div class="chat-section">
          <div class="chat-header">
            <el-icon><ChatDotRound /></el-icon>
            <span>聊天</span>
          </div>
          <div class="chat-messages" ref="chatContainer">
            <div
              v-for="(msg, index) in chatMessages"
              :key="index"
              class="chat-message"
              :class="{ system: msg.isSystem, own: msg.isOwn }"
            >
              <span class="sender" v-if="!msg.isSystem">{{ msg.sender }}:</span>
              <span class="content">{{ msg.content }}</span>
            </div>
          </div>
          <div class="chat-input">
            <el-input
              v-model="chatInput"
              placeholder="输入消息..."
              @keyup.enter="sendChat"
              :disabled="!wsConnected"
            >
              <template #append>
                <el-button @click="sendChat" :disabled="!chatInput.trim() || !wsConnected">
                  <el-icon><Promotion /></el-icon>
                </el-button>
              </template>
            </el-input>
          </div>
        </div>

        <!-- 落子历史 -->
        <div class="move-history">
          <div class="history-header">
            <el-icon><List /></el-icon>
            <span>落子记录</span>
          </div>
          <div class="history-list">
            <div
              v-for="(move, index) in moveHistory"
              :key="index"
              class="history-item"
            >
              <span class="move-number">{{ index + 1 }}.</span>
              <span class="move-color" :class="move.color"></span>
              <span class="move-position">{{ formatPosition(move.position) }}</span>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- 游戏结果弹窗 -->
    <el-dialog
      v-model="showGameResult"
      :title="gameResultTitle"
      width="500px"
      center
      :close-on-click-modal="false"
      :show-close="false"
      class="game-result-dialog"
    >
      <div class="result-content">
        <div class="result-icon" :class="gameResultType">
          <el-icon v-if="gameResultType === 'win'"><TrophyBase /></el-icon>
          <el-icon v-else-if="gameResultType === 'lose'"><CircleClose /></el-icon>
          <el-icon v-else><Connection /></el-icon>
        </div>
        <p class="result-message">{{ gameResultMessage }}</p>
        <div class="result-stats">
          <div class="stat">
            <span class="value">{{ totalMoves }}</span>
            <span class="label">总步数</span>
          </div>
          <div class="stat">
            <span class="value">{{ formatDuration(gameDuration) }}</span>
            <span class="label">用时</span>
          </div>
        </div>

        <!-- 奖励展示 -->
        <div v-if="gameRewards" class="result-rewards">
          <h4>获得奖励</h4>
          <div class="rewards-list">
            <div v-if="gameRewards.gold" class="reward-item">
              <el-icon class="reward-icon gold"><Coin /></el-icon>
              <span class="reward-value">+{{ gameRewards.gold }}</span>
              <span class="reward-label">金币</span>
            </div>
            <div v-if="gameRewards.gems" class="reward-item">
              <el-icon class="reward-icon gems"><Present /></el-icon>
              <span class="reward-value">+{{ gameRewards.gems }}</span>
              <span class="reward-label">宝石</span>
            </div>
            <div v-if="gameRewards.experience" class="reward-item">
              <el-icon class="reward-icon exp"><StarFilled /></el-icon>
              <span class="reward-value">+{{ gameRewards.experience }}</span>
              <span class="reward-label">经验</span>
            </div>
            <div v-if="gameRewards.honor_points" class="reward-item">
              <el-icon class="reward-icon honor"><Medal /></el-icon>
              <span class="reward-value">+{{ gameRewards.honor_points }}</span>
              <span class="reward-label">荣誉</span>
            </div>
          </div>
        </div>

        <!-- 排行榜变化 -->
        <div v-if="leaderboardChange" class="result-leaderboard">
          <h4>排名变化</h4>
          <div class="leaderboard-change">
            <span class="rank-old">#{{ leaderboardChange.old_rank }}</span>
            <el-icon class="rank-arrow" :class="{ up: leaderboardChange.new_rank < leaderboardChange.old_rank }">
              <ArrowLeft v-if="leaderboardChange.new_rank < leaderboardChange.old_rank" />
              <span v-else>→</span>
            </el-icon>
            <span class="rank-new">#{{ leaderboardChange.new_rank }}</span>
            <span v-if="rankImprovement > 0" class="rank-improvement">
              上升 {{ rankImprovement }} 名
            </span>
          </div>
        </div>

        <!-- 解锁的成就 -->
        <div v-if="unlockedAchievements.length > 0" class="result-achievements">
          <h4>解锁成就</h4>
          <div class="achievements-list">
            <div v-for="achievement in unlockedAchievements" :key="achievement.id" class="achievement-item">
              <el-icon class="achievement-icon"><TrophyBase /></el-icon>
              <div class="achievement-info">
                <span class="achievement-name">{{ achievement.name }}</span>
                <el-tag :type="getAchievementTagType(achievement.rarity)" size="small">
                  {{ getRarityText(achievement.rarity) }}
                </el-tag>
              </div>
            </div>
          </div>
        </div>
      </div>
      <template #footer>
        <el-button @click="handleLeave">返回大厅</el-button>
        <el-button type="primary" @click="handlePlayAgain">再来一局</el-button>
      </template>
    </el-dialog>

    <!-- 成就解锁通知组件 -->
    <AchievementUnlock :duration="5000" :max-visible="3" />

    <!-- 游戏结算弹窗 v2.6.0 -->
    <GameSettlementModal v-model="showSettlementModal" />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, ElMessageBox } from 'element-plus'
import {
  ArrowLeft,
  Setting,
  SwitchButton,
  TrophyBase,
  VideoPlay,
  RefreshLeft,
  Connection,
  Flag,
  ChatDotRound,
  Promotion,
  List,
  CircleClose,
  Check,
  CircleCheck,
  Coin,
  Present,
  StarFilled,
  Medal,
} from '@element-plus/icons-vue'
import { useGameStore, useAuthStore, useGameDataStore } from '@/stores'
import { gameDataApi } from '@/api/gamedata.api'
import { useGomokuSocket } from '@/websocket'
import { gameHandlers } from '@/websocket/handlers/game.handler'
import GomokuBoard from '@/components/gomoku/GomokuBoard.vue'
import AchievementUnlock from '@/components/user/AchievementUnlock.vue'
import GameSettlementModal from '@/components/game/GameSettlementModal.vue'
import { GAME_CONFIG } from '@/utils'
import type { Position as PositionType, PlayerColor, GameConfig } from '@/types'

const route = useRoute()
const router = useRouter()
const gameStore = useGameStore()
const authStore = useAuthStore()
const gameDataStore = useGameDataStore()

// WebSocket 连接
const {
  isConnected: wsConnected,
  isAuthenticated: wsAuthenticated,
  joinRoom: wsJoinRoom,
  joinRoomWS,
  enterRoom,
  leaveRoom: wsLeaveRoom,
  makeMove: wsMakeMove,
  resign: wsResign,
  requestDraw: wsRequestDraw,
  respondDraw: wsRespondDraw,
  requestUndo: wsRequestUndo,
  respondUndo: wsRespondUndo,
  sendChat: wsSendChat,
  setReady: wsSetReady,
  startGame: wsStartGame, // v2.3.0: 通过 WebSocket 开始游戏
  on: wsOn,
  off: wsOff,
  // v2.1.0: 不再需要 disconnect，保持连接供大厅复用
  // disconnect: wsDisconnect,
  connect,
  authenticate,
} = useGomokuSocket()

// 从路由获取游戏类型和房间ID
const gameType = computed(() => route.params.gameType as string || 'gomoku')
const roomId = computed(() => route.params.roomId as string)
const currentRoom = computed(() => gameStore.currentRoom)
const currentGame = computed(() => {
  const game = gameStore.currentGame
  console.log('[GameView] currentGame computed accessed:', game)
  return game
})

// 游戏状态
const currentPlayer = ref<PlayerColor>('black')
const board = ref<(PlayerColor | null)[][]>([])
const canMove = ref(false)
const winner = ref<PlayerColor | null>(null)
const lastMove = ref<{ row: number; col: number } | null>(null)
const totalMoves = ref(0)
const blackCaptures = ref(0)
const whiteCaptures = ref(0)
const blackTime = ref(900)
const whiteTime = ref(900)
const gameDuration = ref(0)
const gameStartTime = ref<number | null>(null)
const moveHistory = ref<Array<{ color: PlayerColor; position: PositionType }>>([])

// 游戏配置
const gameConfig = ref<GameConfig>({
  gameMode: 0,
  gameModeStr: '自由模式',
  timeLimit: 900,
  incrementPerMove: 10,
  allowUndo: false,
  allowSpectators: true,
  maxSpectators: 50,
  rankingEnabled: true,
})

// 玩家信息
const players = computed(() => {
  const black = currentGame.value?.players.black || null
  const white = currentGame.value?.players.white || null
  const result = { black, white }
  console.log('[GameView] players computed:', result)
  return result
})

// 游戏状态
const gameStatus = ref<'waiting' | 'ready' | 'playing' | 'ended'>('waiting')
const isGamePlaying = computed(() => gameStatus.value === 'playing')
const isGameWaiting = computed(() => gameStatus.value === 'waiting' || gameStatus.value === 'ready')

// 准备状态
const myReadyState = ref(false)
const opponentReadyState = ref(false)

// 根据玩家颜色确定准备状态
const blackReadyState = computed(() => {
  const myId = authStore.user?.id
  const result = players.value.black?.id === myId ? myReadyState.value : opponentReadyState.value

  console.log('[GameView] blackReadyState computed:', {
    blackPlayerId: players.value.black?.id,
    myId,
    myReadyState: myReadyState.value,
    opponentReadyState: opponentReadyState.value,
    result
  })

  return result
})

const whiteReadyState = computed(() => {
  const myId = authStore.user?.id
  const result = players.value.white?.id === myId ? myReadyState.value : opponentReadyState.value

  console.log('[GameView] whiteReadyState computed:', {
    whitePlayerId: players.value.white?.id,
    myId,
    myReadyState: myReadyState.value,
    opponentReadyState: result,
    result
  })

  return result
})

// v2.3.0: 是否是房主（创建者）
const isCreator = computed(() => {
  const currentRoomInfoCreator = gameStore.currentRoomInfo?.isCreator
  const currentHostId = currentRoom.value?.host?.id
  const myUserId = authStore.user?.id

  console.log('[GameView] isCreator computed:', {
    currentRoomInfoCreator,
    currentHostId,
    myUserId,
    currentRoomInfo: gameStore.currentRoomInfo,
    currentRoom: currentRoom.value,
    hostMatch: currentHostId === myUserId
  })

  // 优先使用 currentRoomInfo.isCreator
  if (currentRoomInfoCreator !== undefined) {
    console.log('[GameView] isCreator result (from currentRoomInfo):', currentRoomInfoCreator)
    return currentRoomInfoCreator
  }
  // 备用：检查 currentRoom.host
  const result = currentHostId === myUserId
  console.log('[GameView] isCreator result (from host):', result)
  return result
})

// v2.3.0: 是否可以开始游戏
// 条件：1. 是房主 2. 有2名玩家 3. 所有人都准备 4. 游戏状态为 ready
const canStartGame = computed(() => {
  const result = isCreator.value &&
    players.value.black &&
    players.value.white &&
    blackReadyState.value &&
    whiteReadyState.value &&
    (gameStatus.value === 'ready' || gameStatus.value === 'waiting')

  console.log('[GameView] canStartGame computed:', {
    isCreator: isCreator.value,
    hasBlackPlayer: !!players.value.black,
    hasWhitePlayer: !!players.value.white,
    blackReady: blackReadyState.value,
    whiteReady: whiteReadyState.value,
    gameStatus: gameStatus.value,
    result
  })

  return result
})

const isMyTurn = computed(() => {
  if (!isGamePlaying.value || !authStore.user) return false
  const myColor = players.value.black?.id === authStore.user.id ? 'black' :
    players.value.white?.id === authStore.user.id ? 'white' : null
  return myColor === currentPlayer.value
})

const gameStatusType = computed(() => {
  switch (gameStatus.value) {
    case 'waiting': return 'info'
    case 'ready': return 'warning'
    case 'playing': return 'success'
    case 'ended': return 'danger'
    default: return 'info'
  }
})

const gameStatusText = computed(() => {
  switch (gameStatus.value) {
    case 'waiting': return '等待玩家'
    case 'ready': return '准备中'
    case 'playing': return '对弈中'
    case 'ended': return '已结束'
    default: return '未知'
  }
})

// UI 状态
const showSettings = ref(false)
const showGameResult = ref(false)
const starting = ref(false)
const readying = ref(false)
const chatInput = ref('')
const chatMessages = ref<Array<{
  sender: string
  content: string
  isSystem?: boolean
  isOwn?: boolean
}>>([])
const chatContainer = ref<HTMLElement | null>(null)

// 游戏结果
const gameResultType = ref<'win' | 'lose' | 'draw'>('draw')
const gameResultTitle = computed(() => {
  switch (gameResultType.value) {
    case 'win': return '胜利!'
    case 'lose': return '失败'
    case 'draw': return '平局'
    default: return '游戏结束'
  }
})
const gameResultMessage = ref('')

// v2.0.3: 游戏结算数据
const gameRewards = ref<{
  gold?: number
  gems?: number
  experience?: number
  honor_points?: number
} | null>(null)

const leaderboardChange = ref<{
  old_rank: number
  new_rank: number
  type?: string
} | null>(null)

const unlockedAchievements = ref<Array<{
  id: string
  name: string
  rarity: string
}>>([])

const rankImprovement = computed(() => {
  if (!leaderboardChange.value) return 0
  return leaderboardChange.value.old_rank - leaderboardChange.value.new_rank
})

// v2.6.0: 游戏结算弹窗状态
const showSettlementModal = computed({
  get: () => gameStore.showSettlementModal,
  set: (value: boolean) => {
    if (!value) {
      gameStore.closeSettlementModal()
    }
  },
})

const getRarityText = (rarity: string): string => {
  const rarityMap: Record<string, string> = {
    COMMON: '普通',
    RARE: '稀有',
    EPIC: '史诗',
    LEGENDARY: '传说',
  }
  return rarityMap[rarity] || rarity
}

const getAchievementTagType = (rarity: string): 'info' | 'success' | 'warning' | 'danger' => {
  const typeMap: Record<string, 'info' | 'success' | 'warning' | 'danger'> = {
    COMMON: 'info',
    RARE: 'success',
    EPIC: 'warning',
    LEGENDARY: 'danger',
  }
  return typeMap[rarity] || 'info'
}

// 初始化棋盘
const initBoard = () => {
  const size = GAME_CONFIG.BOARD_SIZE
  board.value = Array(size).fill(null).map(() => Array(size).fill(null))
}

// 格式化时间
const formatTime = (seconds: number) => {
  const mins = Math.floor(seconds / 60)
  const secs = seconds % 60
  return `${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`
}

// 格式化时长
const formatDuration = (seconds: number) => {
  const mins = Math.floor(seconds / 60)
  const secs = seconds % 60
  return `${mins}分${secs}秒`
}

// 格式化位置
const formatPosition = (pos: PositionType) => {
  const colLetter = String.fromCharCode(65 + pos.y)
  return `${colLetter}${pos.x + 1}`
}

// 落子
const handleMove = async (position: PositionType) => {
  if (!canMove.value || !isMyTurn.value) return

  const originalPlayer = currentPlayer.value

  // 通过 WebSocket 发送落子
  wsMakeMove({ row: position.x, col: position.y })

  console.log('[GameView] handleMove:', {
    position,
    currentPlayer: originalPlayer,
    myId: authStore.user?.id,
    blackPlayerId: players.value.black?.id,
    whitePlayerId: players.value.white?.id,
    canMove: canMove.value,
    isMyTurn: isMyTurn.value
  })

  // 乐观更新 UI（实际状态由服务器确认）
  board.value[position.x][position.y] = currentPlayer.value
  lastMove.value = { row: position.x, col: position.y }
  totalMoves.value++

  moveHistory.value.push({
    color: currentPlayer.value,
    position,
  })

  // 暂时移除这个乐观更新，让服务器返回的消息来正确更新
  // currentPlayer.value = currentPlayer.value === 'black' ? 'white' : 'black'
  canMove.value = false
}

// 开始游戏
// v2.3.0: 通过 WebSocket 发送 start_game 操作
const handleStartGame = () => {
  if (!canStartGame.value) {
    // 提供具体的错误提示
    if (!isCreator.value) {
      ElMessage.warning('只有房主可以开始游戏')
    } else if (!players.value.black || !players.value.white) {
      ElMessage.warning('等待其他玩家加入')
    } else if (!blackReadyState.value || !whiteReadyState.value) {
      ElMessage.warning('等待所有玩家准备')
    }
    return
  }

  // 重置游戏时长
  gameStartTime.value = null
  gameDuration.value = 0

  starting.value = true

  try {
    // 通过 WebSocket 发送开始游戏请求
    wsStartGame(roomId.value)
    // 注意：游戏状态会在收到 game_started 广播后更新
    addSystemMessage('正在开始游戏...')
  } catch (error: any) {
    ElMessage.error(error.message || '开始游戏失败')
    starting.value = false
  }
  // starting.value 会在收到 game_started 或超时后重置
}

// 准备/取消准备
const handleReady = () => {
  const newReadyState = !myReadyState.value
  readying.value = true

  try {
    wsSetReady(newReadyState)
    myReadyState.value = newReadyState

    if (newReadyState) {
      addSystemMessage('您已准备就绪')
    } else {
      addSystemMessage('您取消了准备')
    }
  } catch (error: any) {
    ElMessage.error(error.message || '操作失败')
  } finally {
    readying.value = false
  }
}

// 悔棋
const handleUndo = async () => {
  if (!gameConfig.value.allowUndo) {
    ElMessage.warning('本局不允许悔棋')
    return
  }
  try {
    wsRequestUndo()
    ElMessage.success('悔棋请求已发送')
  } catch (error: any) {
    ElMessage.error(error.message || '悔棋失败')
  }
}

// 求和
const handleDrawOffer = async () => {
  try {
    wsRequestDraw()
    ElMessage.success('求和请求已发送')
  } catch (error: any) {
    ElMessage.error(error.message || '求和失败')
  }
}

// 认输
// v2.4.0: 通过 WebSocket 发送认输消息
const handleSurrender = async () => {
  try {
    await ElMessageBox.confirm('确定要认输吗？', '确认', {
      type: 'warning',
    })
    // 发送认输消息到服务器
    wsResign()
    addSystemMessage('您已认输')
  } catch {
    // 用户取消
  }
}

/**
 * 同步游戏结果到 Game Data Service
 * 根据文档，游戏结束后应该更新：
 * - 经验值
 * - 等级（如满足升级条件）
 * - 成就进度
 * - 排行榜分数
 */
const syncGameResult = async (result: 'win' | 'lose' | 'draw') => {
  const userId = authStore.user?.id
  if (!userId) return

  try {
    // 计算本局获得的经验值（示例逻辑）
    const baseExp = 100
    const expGained = result === 'win' ? baseExp * 2 :
                      result === 'draw' ? baseExp :
                      Math.floor(baseExp * 0.5)

    // 更新游戏档案（经验值）
    const currentExp = gameDataStore.experience || 0
    const newExp = currentExp + expGained

    // 调用 Game Data Service 更新
    await gameDataApi.updateGameProfile(userId, {
      experience_points: newExp,
      // 其他字段由服务端计算（等级、成就等）
    })

    // 重新加载游戏数据以获取最新状态（带 game_type 参数）
    const displayName = authStore.user?.username || undefined
    await gameDataStore.loadAllGameData(userId, displayName, gameType.value)

    console.log(`[GameView] Game result synced: ${result}, exp gained: ${expGained}`)
  } catch (error) {
    console.error('[GameView] Failed to sync game result:', error)
    // 不显示错误给用户，静默失败
  }
}

// 离开房间
// v2.4.6: 如果游戏正在进行中，离开房间视为认输
const handleLeave = async () => {
  try {
    // 如果游戏正在进行中，需要先确认
    if (isGamePlaying.value) {
      try {
        await ElMessageBox.confirm('游戏正在进行中，离开将被视为认输。确定要离开吗？', '确认离开', {
          type: 'warning',
          confirmButtonText: '确定离开',
          cancelButtonText: '继续游戏',
        })
        // 发送认输消息到服务器
        wsResign()
        addSystemMessage('您已认输并离开房间')
      } catch {
        // 用户取消，继续游戏
        return
      }
    }

    // v2.1.0: 通过 WebSocket 离开房间
    wsLeaveRoom()
    // 清理本地状态
    gameStore.clearCurrentRoom()
    // 跳转回大厅（保持 WebSocket 连接）
    router.push(`/game/${gameType.value}/lobby`)
  } catch (error) {
    // 即使失败也跳转
    router.push(`/game/${gameType.value}/lobby`)
  }
}

// 再来一局
const handlePlayAgain = () => {
  showGameResult.value = false
  initBoard()
  gameStatus.value = 'waiting'
  currentPlayer.value = 'black'
  winner.value = null
  lastMove.value = null
  totalMoves.value = 0
  moveHistory.value = []
  blackTime.value = gameConfig.value.timeLimit
  whiteTime.value = gameConfig.value.timeLimit
}

// v2.3.0: 同步游戏状态到本地组件状态
const syncGameStateToLocal = (data: any) => {
  if (!data) return

  // v2.4.4: 同步时间（兼容两种位置：data.blackTimeLeft 或 data.board.blackTimeLeft）
  if (data.blackTimeLeft !== undefined) {
    blackTime.value = data.blackTimeLeft
  } else if (data.board?.blackTimeLeft !== undefined) {
    blackTime.value = data.board.blackTimeLeft
  }

  if (data.whiteTimeLeft !== undefined) {
    whiteTime.value = data.whiteTimeLeft
  } else if (data.board?.whiteTimeLeft !== undefined) {
    whiteTime.value = data.board.whiteTimeLeft
  }

  // 同步棋盘数据
  if (data.board) {
    const boardData = data.board
    if (boardData.totalMoves !== undefined) {
      totalMoves.value = boardData.totalMoves
    }
    // 关键：同步当前玩家（用于回合切换）
    if (boardData.currentPlayer !== undefined) {
      const newPlayer = boardData.currentPlayer === 1 ? 'black' : 'white'
      if (newPlayer !== currentPlayer.value) {
        console.log('[GameView] currentPlayer switched:', currentPlayer.value, '->', newPlayer)
        currentPlayer.value = newPlayer
      }
    }
    if (boardData.lastMove) {
      lastMove.value = boardData.lastMove
    }
  }

  // 同步当前回合（有些消息直接在 data 中）
  if (data.currentPlayer !== undefined && data.board === undefined) {
    const newPlayer = data.currentPlayer === 1 || data.currentPlayer === 'black' ? 'black' : 'white'
    if (newPlayer !== currentPlayer.value) {
      console.log('[GameView] currentPlayer switched (from data):', currentPlayer.value, '->', newPlayer)
      currentPlayer.value = newPlayer
    }
  }

  // 更新 canMove 状态
  canMove.value = isMyTurn.value

  console.log('[GameView] Game state synced to local:', {
    totalMoves: totalMoves.value,
    currentPlayer: currentPlayer.value,
    canMove: canMove.value,
    isMyTurn: isMyTurn.value,
    blackTime: blackTime.value,
    whiteTime: whiteTime.value
  })
}

// 发送聊天消息
const sendChat = () => {
  if (!chatInput.value.trim()) return

  // 通过 WebSocket 发送聊天消息
  wsSendChat(chatInput.value.trim())

  chatInput.value = ''

  nextTick(() => {
    if (chatContainer.value) {
      chatContainer.value.scrollTop = chatContainer.value.scrollHeight
    }
  })
}

// 添加系统消息
const addSystemMessage = (content: string) => {
  chatMessages.value.push({
    sender: '系统',
    content,
    isSystem: true,
  })

  nextTick(() => {
    if (chatContainer.value) {
      chatContainer.value.scrollTop = chatContainer.value.scrollHeight
    }
  })
}

// 计时器
let timer: ReturnType<typeof setInterval> | null = null

/**
 * 设置 WebSocket 事件监听
 */
const setupWebSocketListeners = () => {
  // v2.4.1: 先清理旧的监听器，防止重复监听
  // （可能在热更新或组件重新挂载时发生）
  wsOff('room_info')
  wsOff('room_config')
  wsOff('current_state')
  wsOff('player_piece_assigned')
  wsOff('operation_result')
  wsOff('game_started')
  wsOff('player_ready')
  wsOff('ready_confirm')
  wsOff('all_players_ready')
  wsOff('turn_changed')
  wsOff('piece_placed')
  wsOff('move_result')
  wsOff('game_state_update')
  wsOff('game_state')
  wsOff('game_end')
  wsOff('player_joined')
  wsOff('player_left')
  wsOff('chat')
  wsOff('player_surrendered')
  wsOff('draw_offer')
  wsOff('draw_response')
  wsOff('undo_request')
  wsOff('move_undone')
  wsOff('time_update')
  wsOff('connect')
  wsOff('disconnect')
  wsOff('auth_success')
  wsOff('room_joined')  // v2.5.3: 添加 room_joined 清理
  wsOff('game_settlement')  // v2.6.0: 添加 game_settlement 清理

  // v2.5.3: 房间加入成功 - 服务端现在发送 room_joined 而不是 room_info
  wsOn('room_joined', (message: any) => {
    console.log('[GameView] room_joined received:', message.data)
    console.log('[GameView] room_joined - pieceType:', message.data?.pieceType)
    console.log('[GameView] room_joined - playerId:', message.data?.playerId)

    gameHandlers.handleRoomJoined(message)

    console.log('[GameView] After handleRoomJoined, currentRoomInfo:', gameStore.currentRoomInfo)

    // 设置我的棋子（通过 gameStore）
    if (message.data?.pieceType) {
      gameStore.setMyPiece(message.data.pieceType)
      console.log('[GameView] My piece set to:', message.data.pieceType)
    }

    // 如果有房间信息，也处理玩家列表
    if (message.data?.roomInfo) {
      addSystemMessage(`成功加入房间`)
    }
  })

  // 房间信息更新 - 包含玩家列表（兼容旧格式）
  wsOn('room_info', (message: any) => {
    console.log('[GameView] room_info received:', message.data)
    console.log('[GameView] room_info - creator_id:', message.data?.creator_id)
    console.log('[GameView] room_info - my_id:', authStore.user?.id)
    console.log('[GameView] room_info - isCreator should be:', message.data?.creator_id === authStore.user?.id)

    gameHandlers.handleRoomInfo(message)

    console.log('[GameView] After handleRoomInfo, currentRoomInfo:', gameStore.currentRoomInfo)

    // 更新本地玩家显示
    if (message.data?.players) {
      const myId = authStore.user?.id
      const players = message.data.players

      // 查找当前玩家并更新显示
      const myPlayer = players.find((p: any) => p.player_id === myId)
      if (myPlayer) {
        addSystemMessage(`你已在房间中`)
      }
    }
  })

  // 房间配置更新
  wsOn('room_config', (message: any) => {
    console.log('[GameView] room_config received:', message.data)
    gameHandlers.handleRoomConfig(message)
    
    // 同步游戏配置到组件本地状态
    if (message.data) {
      Object.assign(gameConfig.value, message.data)
      console.log('[GameView] Room config synced to component:', gameConfig.value)
    }
  })

  // 游戏状态更新 - 包含 playerPieces
  wsOn('current_state', (message: any) => {
    console.log('[GameView] current_state received:', message.data)
    gameHandlers.handleGameState(message)

    // 同步游戏状态到组件本地状态
    if (message.data?.board) {
      const boardData = message.data.board
      if (boardData.totalMoves !== undefined) {
        totalMoves.value = boardData.totalMoves
      }
      if (boardData.blackTimeLeft !== undefined) {
        blackTime.value = boardData.blackTimeLeft
      }
      if (boardData.whiteTimeLeft !== undefined) {
        whiteTime.value = boardData.whiteTimeLeft
      }
      if (boardData.currentPlayer) {
        currentPlayer.value = boardData.currentPlayer === 1 ? 'black' : 'white'
      }
      if (boardData.lastMove) {
        lastMove.value = boardData.lastMove
      }
      console.log('[GameView] Game state synced:', {
        totalMoves: totalMoves.value,
        blackTime: blackTime.value,
        whiteTime: whiteTime.value,
        currentPlayer: currentPlayer.value,
      })
    }

    // 同步游戏配置
    if (message.data?.config) {
      Object.assign(gameConfig.value, message.data.config)
      console.log('[GameView] Game config synced:', gameConfig.value)
    }

    // 更新玩家棋子信息
    if (message.data?.playerPieces) {
      const myId = authStore.user?.id
      const pieces = message.data.playerPieces

      // 根据棋子分配更新玩家显示
      Object.entries(pieces).forEach(([playerId, piece]) => {
        const pieceType = piece === 1 ? 'black' : 'white'
        if (playerId === myId) {
          gameStore.setMyPiece(pieceType)
        }
      })
    }
  })

  // 玩家棋子分配
  wsOn('player_piece_assigned', (message: any) => {
    console.log('[GameView] player_piece_assigned received:', message.data)
    gameHandlers.handlePlayerPieceAssigned(message)
  })

  // v2.3.0: 操作结果处理
  wsOn('operation_result', (message: any) => {
    console.log('[GameView] operation_result received:', message)
    const { operation, success, error, message: msg } = message.data || {}

    if (operation === 'start_game') {
      if (!success) {
        starting.value = false
        ElMessage.error(msg || error || '开始游戏失败')
      }
    }
  })

  // 游戏开始
  // v2.3.0: 更新游戏状态，重置 starting 状态
  wsOn('game_started', (message: any) => {
    console.log('[GameView] game_started received:', message)
    starting.value = false
    gameStatus.value = 'playing'
    canMove.value = isMyTurn.value

    // 记录游戏开始时间
    gameStartTime.value = Date.now()
    gameDuration.value = 0

    // 如果消息包含棋盘数据，更新棋盘
    if (message.data?.board) {
      gameHandlers.handleGameStarted(message)
    }

    addSystemMessage('游戏开始！黑方先行')
  })

  // 玩家准备状态更新
  wsOn('player_ready', (message: any) => {
    console.log('[GameView] player_ready received:', message)
    const { playerId: readyPlayerId, ready } = message.data
    const myId = authStore.user?.id

    console.log('[GameView] Player ready update:', { readyPlayerId, ready, myId })

    if (readyPlayerId === myId) {
      myReadyState.value = ready
      console.log('[GameView] My ready state updated:', ready)
    } else {
      opponentReadyState.value = ready
      console.log('[GameView] Opponent ready state updated:', ready)
    }

    // 如果双方都准备好，更新游戏状态
    if (blackReadyState.value && whiteReadyState.value) {
      gameStatus.value = 'ready'
    }
  })

  // 准备确认
  wsOn('ready_confirm', (message: any) => {
    const { playerId: readyPlayerId, ready } = message.data
    const myId = authStore.user?.id

    console.log('[GameView] ready_confirm received:', { readyPlayerId, ready, myId })

    if (readyPlayerId === myId) {
      myReadyState.value = ready
      console.log('[GameView] My ready state updated (from ready_confirm):', ready)
    } else {
      opponentReadyState.value = ready
      console.log('[GameView] Opponent ready state updated (from ready_confirm):', ready)
    }

    // 如果双方都准备好，更新游戏状态
    if (blackReadyState.value && whiteReadyState.value) {
      gameStatus.value = 'ready'
    }

    addSystemMessage(`${ready ? '已准备' : '取消准备'}`)
  })

  // 所有玩家准备状态
  wsOn('all_players_ready', (message: any) => {
    const { players: readyPlayers, canStart } = message.data
    if (readyPlayers) {
      Object.entries(readyPlayers).forEach(([playerId, ready]) => {
        const myId = authStore.user?.id
        if (playerId === myId) {
          myReadyState.value = ready as boolean
        } else {
          opponentReadyState.value = ready as boolean
        }
      })
    }

    if (canStart) {
      gameStatus.value = 'ready'
    }
  })

  // 轮次更新
  wsOn('turn_changed', (data: { current_player: PlayerColor }) => {
    console.log('[GameView] turn_changed received:', data.current_player)
    currentPlayer.value = data.current_player
    canMove.value = isMyTurn.value
    console.log('[GameView] After turn_changed:', {
      currentPlayer: currentPlayer.value,
      myId: authStore.user?.id,
      blackPlayerId: players.value.black?.id,
      whitePlayerId: players.value.white?.id,
      isMyTurn: isMyTurn.value,
      canMove: canMove.value
    })
  })

  // 收到落子
  wsOn('piece_placed', (data: any) => {
    console.log('[GameView] piece_placed received:', data)

    // 兼容两种坐标格式：{ x, y } 和 { row, col }
    const x = data.position?.x !== undefined ? data.position.x : data.position?.row
    const y = data.position?.y !== undefined ? data.position.y : data.position?.col

    console.log('[GameView] Position parsed:', { x, y, original: data.position })

    if (x !== undefined && y !== undefined) {
      board.value[x][y] = data.color
      lastMove.value = { row: x, col: y }
      totalMoves.value++
      moveHistory.value.push({
        color: data.color,
        position: { x, y },
      })

      console.log('[GameView] After piece_placed:', {
        position: { x, y },
        pieceColor: data.color,
        boardUpdated: board.value[x][y],
        currentPlayer: currentPlayer.value
      })
    } else {
      console.error('[GameView] Invalid position in piece_placed:', data.position)
    }
  })

  // v2.3.0: 处理落子结果（服务端实际发送的消息类型）
  wsOn('move_result', (message: any) => {
    console.log('[GameView] move_result received:', message)
    if (message.data?.success && message.data?.position) {
      // 兼容两种坐标格式：{ row, col } 和 { x, y }
      const row = message.data.position.row !== undefined ? message.data.position.row : message.data.position.x
      const col = message.data.position.col !== undefined ? message.data.position.col : message.data.position.y

      console.log('[GameView] Position parsed (move_result):', { row, col, original: message.data.position })

      // 根据 playerId 确定棋子颜色（不依赖 currentPlayer，因为可能已经被 game_state 更新）
      const playerId = message.data.playerId
      const playerPiecesMap = gameStore.playerPieces
      let piece: PlayerColor = currentPlayer.value // 默认值

      if (playerId && playerPiecesMap[playerId]) {
        piece = playerPiecesMap[playerId] as PlayerColor
        console.log('[GameView] Piece color from playerPieces:', { playerId, piece })
      } else {
        // 备用：根据当前玩家确定（可能不准确）
        console.warn('[GameView] Cannot determine piece color from playerPieces, using currentPlayer')
      }

      // 更新本地棋盘状态
      board.value[row][col] = piece
      lastMove.value = { row, col }
      totalMoves.value++

      // 提示消息
      addSystemMessage(`${piece === 'black' ? '黑方' : '白方'}落子 (${row}, ${col})`)

      // 注意：不在这里切换 currentPlayer，让 game_state 消息来控制回合切换
      // 这样可以确保状态由服务端统一控制
    }
    // 调用 handler 更新 store
    gameHandlers.handleMoveResult(message)
  })

  // v2.3.0: 处理游戏状态更新（服务端广播给所有玩家）
  wsOn('game_state_update', (message: any) => {
    console.log('[GameView] game_state_update received:', message)
    if (message.data) {
      gameHandlers.handleGameState(message)
      // 同步本地状态
      syncGameStateToLocal(message.data)
    }
  })

  // v2.3.0: 处理游戏状态消息（服务端发送的主要状态更新）
  wsOn('game_state', (message: any) => {
    console.log('[GameView] game_state received:', message)
    if (message.data) {
      gameHandlers.handleGameState(message)
      // 同步本地状态
      syncGameStateToLocal(message.data)
    }
  })

  // 游戏结束
  wsOn('game_end', async (message: any) => {
    console.log('[GameView] game_end received:', message)
    const data = message.data
    gameStatus.value = 'ended'

    // 计算游戏时长
    if (gameStartTime.value) {
      const endTime = Date.now()
      gameDuration.value = Math.floor((endTime - gameStartTime.value) / 1000)
      console.log('[GameView] Game duration calculated:', {
        startTime: gameStartTime.value,
        endTime,
        duration: gameDuration.value
      })
    } else if (data.duration !== undefined) {
      // 如果服务端返回了时长，使用服务端数据
      gameDuration.value = data.duration
      console.log('[GameView] Game duration from server:', data.duration)
    } else {
      console.warn('[GameView] Game duration not available')
    }

    // 判断我的颜色
    const myColor = players.value.black?.id === authStore.user?.id ? 'black' :
                    players.value.white?.id === authStore.user?.id ? 'white' : null

    console.log('[GameView] Game end - my color:', myColor, 'data:', data)

    // v2.4.5: 判断是否是平局（兼容多种格式）
    const isDraw =
      data.result === 'draw' ||
      data.result === 0 ||
      data.result === '0' ||
      data.isDraw === true ||
      data.is_draw === true ||
      data.reason === 'draw' ||
      data.reason === 'agreed_draw' ||
      data.reason === 'mutual_draw' ||
      (data.winnerPiece === 0 || data.winnerPiece === '0') ||
      (data.winner === 'none' || data.winner === null || data.winner === '')

    console.log('[GameView] Is draw:', isDraw, 'reason:', data.reason, 'result:', data.result, 'winnerPiece:', data.winnerPiece)

    let winnerColor: PlayerColor | null = null

    if (isDraw) {
      // 平局
      gameResultType.value = 'draw'
      gameResultMessage.value = '双方平局'
      winner.value = null
      console.log('[GameView] Game result: DRAW')
    } else {
      // 获取获胜方颜色（兼容数字和字符串格式）
      if (data.winnerPiece !== undefined) {
        if (data.winnerPiece === 1 || data.winnerPiece === '1' || data.winnerPiece === 'black') {
          winnerColor = 'black'
        } else if (data.winnerPiece === 2 || data.winnerPiece === '2' || data.winnerPiece === 'white') {
          winnerColor = 'white'
        }
      } else if (data.result !== undefined) {
        if (data.result === 1 || data.result === '1') {
          winnerColor = 'black'
        } else if (data.result === 2 || data.result === '2') {
          winnerColor = 'white'
        }
      } else if (data.winner !== undefined) {
        if (data.winner === 'black' || data.winner === 1 || data.winner === '1') {
          winnerColor = 'black'
        } else if (data.winner === 'white' || data.winner === 2 || data.winner === '2') {
          winnerColor = 'white'
        }
      }

      winner.value = winnerColor
      console.log('[GameView] Winner color determined:', winnerColor)

      // 判断结果
      if (winnerColor === myColor) {
        gameResultType.value = 'win'
        gameResultMessage.value = '恭喜您获得胜利！'
      } else {
        gameResultType.value = 'lose'
        gameResultMessage.value = '很遗憾，您输了'
      }

      console.log('[GameView] Game result:', {
        myColor,
        winnerColor,
        resultType: gameResultType.value,
        message: gameResultMessage.value
      })
    }

    // 处理游戏结算数据 (v2.0.3)
    if (data.rewards) {
      gameRewards.value = data.rewards
    }
    if (data.leaderboard_changes) {
      leaderboardChange.value = data.leaderboard_changes
    }
    if (data.achievements) {
      unlockedAchievements.value = data.achievements
    }

    showGameResult.value = true

    // 同步游戏结果
    await syncGameResult(gameResultType.value)
  })

  // v2.6.0: 处理游戏结算消息
  wsOn('game_settlement', (message: any) => {
    console.log('[GameView] ===== game_settlement received =====')
    console.log('[GameView] Message type:', message?.type)
    console.log('[GameView] Message data:', message?.data)
    console.log('[GameView] Full message:', message)
    gameHandlers.handleGameSettlement(message)
  })

  // 玩家加入
  wsOn('player_joined', (message: any) => {
    console.log('[GameView] player_joined received:', message)

    // 先调用 game handler 更新玩家信息
    gameHandlers.handlePlayerJoined(message)

    const data = message.data || message
    // 兼容多种消息格式
    const playerInfo = data.player || data.playerInfo || { username: data.playerId || '玩家' }
    const username = playerInfo.username || playerInfo.displayName || '玩家'
    addSystemMessage(`${username} 加入了游戏`)
    console.log('[GameView] Current players after join:', players.value)
    // 更新游戏状态
    if (players.value.black && players.value.white) {
      gameStatus.value = 'waiting'
    }
  })

  // 玩家离开
  wsOn('player_left', (message: any) => {
    console.log('[GameView] player_left received:', message)

    // 先调用 game handler 更新玩家信息
    gameHandlers.handlePlayerLeft(message)

    const data = message.data || message
    // 兼容多种消息格式
    const playerInfo = data.player || data.playerInfo || { username: data.playerId || '玩家' }
    const username = playerInfo.username || playerInfo.displayName || '玩家'
    addSystemMessage(`${username} 离开了游戏`)
    console.log('[GameView] Current players after leave:', players.value)
    // 重置准备状态
    opponentReadyState.value = false
    gameStatus.value = 'waiting'
  })

  // 收到聊天消息
  wsOn('chat', (message: any) => {
    const data = message.data
    chatMessages.value.push({
      sender: data.playerId,
      content: data.message,
      isOwn: data.playerId === authStore.user?.id,
    })
    nextTick(() => {
      if (chatContainer.value) {
        chatContainer.value.scrollTop = chatContainer.value.scrollHeight
      }
    })
  })

  // v2.4.0: 认输广播（文档定义：player_surrendered）
  wsOn('player_surrendered', (message: any) => {
    console.log('[GameView] player_surrendered received:', message)
    const { playerId: surrenderPlayerId, piece } = message.data || message

    // 确定认输方的颜色
    const surrenderColor = piece === 1 ? '黑方' : '白方'
    addSystemMessage(`${surrenderColor}认输了`)

    // 如果是对手认输，显示胜利提示
    if (surrenderPlayerId !== authStore.user?.id) {
      ElMessage.success('对手认输，您获胜了！')
    }
  })

  // v2.4.0: 求和请求（文档定义：draw_offer）
  wsOn('draw_offer', (message: any) => {
    console.log('[GameView] draw_offer received:', message)

    ElMessageBox.confirm('对手请求求和，是否接受？', '求和请求', {
      confirmButtonText: '接受',
      cancelButtonText: '拒绝',
      type: 'info',
      closeOnClickModal: false,  // 禁止点击遮罩层关闭
      closeOnPressEscape: false, // 禁止按 ESC 关闭
      distinguishCancelAndClose: true, // 区分取消和关闭
    }).then(() => {
      // 接受求和
      wsRespondDraw(true)
      addSystemMessage('您接受了求和请求')
    }).catch((action: string) => {
      // 只有点击"拒绝"按钮才发送拒绝消息
      if (action === 'cancel') {
        wsRespondDraw(false)
        addSystemMessage('您拒绝了求和请求')
      }
      // 如果是点击遮罩层关闭 (action === 'close')，不做任何操作
    })
  })

  // v2.4.0: 和棋响应结果
  // 注意：这个消息是广播给双方的，但只有发起求和的一方需要处理
  // 响应求和的一方已经在点击"接受/拒绝"时显示了相应消息
  wsOn('draw_response', (message: any) => {
    console.log('[GameView] draw_response received:', message)
    const data = message.data || message
    const accepted = data.accepted
    const fromPlayer = data.fromPlayer || data.playerId

    // 详细日志，便于调试
    console.log('[GameView] draw_response data:', data)
    console.log('[GameView] draw_response accepted:', accepted, typeof accepted)
    console.log('[GameView] draw_response fromPlayer:', fromPlayer, 'myId:', authStore.user?.id)

    // 如果 fromPlayer 是当前用户，说明我是响应者，不需要再显示消息
    // 我已经在点击"接受/拒绝"时显示了相应消息
    if (fromPlayer === authStore.user?.id) {
      console.log('[GameView] I am the responder, skipping draw_response display')
      return
    }

    // 兼容多种格式：true, 'true', 1, '1'
    const isAccepted = accepted === true || accepted === 'true' || accepted === 1 || accepted === '1'

    if (isAccepted) {
      addSystemMessage('对手接受了求和请求')
      // 游戏将以平局结束，等待 game_end 消息
    } else {
      // 只有明确拒绝时才显示拒绝消息
      // 避免因字段缺失或类型问题误显示拒绝
      const isRejected = accepted === false || accepted === 'false' || accepted === 0 || accepted === '0'
      if (isRejected) {
        addSystemMessage('对手拒绝了求和请求')
      } else {
        // 其他情况（如字段缺失）记录警告，不显示消息
        console.warn('[GameView] draw_response has unclear accepted value:', accepted)
      }
    }
  })

  // v2.4.0: 悔棋请求（文档定义：undo_request）
  wsOn('undo_request', (message: any) => {
    console.log('[GameView] undo_request received:', message)

    ElMessageBox.confirm('对手请求悔棋，是否同意？', '悔棋请求', {
      confirmButtonText: '同意',
      cancelButtonText: '拒绝',
      type: 'info',
      closeOnClickModal: false,  // 禁止点击遮罩层关闭
      closeOnPressEscape: false, // 禁止按 ESC 关闭
      distinguishCancelAndClose: true, // 区分取消和关闭
    }).then(() => {
      // 同意悔棋
      wsRespondUndo(true)
      addSystemMessage('您同意了悔棋请求')
    }).catch((action: string) => {
      // 只有点击"拒绝"按钮才发送拒绝消息
      if (action === 'cancel') {
        wsRespondUndo(false)
        addSystemMessage('您拒绝了悔棋请求')
      }
      // 如果是点击遮罩层关闭 (action === 'close')，不做任何操作
    })
  })

  // v2.4.0: 悔棋完成
  wsOn('move_undone', (message: any) => {
    console.log('[GameView] move_undone received:', message)
    const { board: newBoard, totalMoves: newTotalMoves, lastPosition } = message.data || message

    // 更新棋盘状态
    if (newBoard) {
      // 转换棋盘格式：数字 -> 颜色字符串
      for (let i = 0; i < newBoard.length; i++) {
        for (let j = 0; j < newBoard[i].length; j++) {
          if (newBoard[i][j] === 0) {
            board.value[i][j] = null
          } else if (newBoard[i][j] === 1) {
            board.value[i][j] = 'black'
          } else if (newBoard[i][j] === 2) {
            board.value[i][j] = 'white'
          }
        }
      }
    }

    // 更新总步数
    if (newTotalMoves !== undefined) {
      totalMoves.value = newTotalMoves
    }

    // 移除最后一步的落子记录
    if (moveHistory.value.length > 0) {
      moveHistory.value.pop()
    }

    // 更新最后落子位置
    if (lastPosition) {
      lastMove.value = lastPosition
    } else {
      lastMove.value = null
    }

    addSystemMessage('悔棋成功')
  })

  // v2.4.4: 时间更新
  wsOn('time_update', (message: any) => {
    const data = message.data || message

    // 兼容多种字段名格式（服务器可能使用 blackTime 或 blackTimeLeft）
    const blackTimeLeft = data.blackTimeLeft ?? data.black_time_left ?? data.blackTime ?? data.black_time
    const whiteTimeLeft = data.whiteTimeLeft ?? data.white_time_left ?? data.whiteTime ?? data.white_time

    if (blackTimeLeft !== undefined) {
      blackTime.value = blackTimeLeft
    }
    if (whiteTimeLeft !== undefined) {
      whiteTime.value = whiteTimeLeft
    }
  })

  // 连接状态
  wsOn('connect', () => {
    addSystemMessage('已连接到游戏服务器')
  })

  wsOn('disconnect', () => {
    addSystemMessage('与游戏服务器断开连接')
  })

  // 认证成功
  wsOn('auth_success', () => {
    addSystemMessage('认证成功')
  })
}

onMounted(async () => {
  console.log('[GameView] Component mounted')
  console.log('[GameView] Initial state:', {
    players: players.value,
    totalMoves: totalMoves.value,
    blackTime: blackTime.value,
    whiteTime: whiteTime.value,
    gameConfig: gameConfig.value,
    currentGame: gameStore.currentGame,
  })
  
  initBoard()
  canMove.value = false

  // 设置 WebSocket 事件监听
  setupWebSocketListeners()

  // v2.1.0: 复用已建立的 WebSocket 连接
  const accessToken = authStore.accessToken
  const userId = authStore.user?.id

  // v2.3.0: 检查是否已经在房间中（可能在挂载前就收到了 room_info 消息）
  if (gameStore.currentRoomInfo?.id === roomId.value) {
    console.log('[GameView] Room info already exists before mounting, processing...')
    console.log('[GameView] currentRoomInfo:', gameStore.currentRoomInfo)

    // 手动触发一次玩家信息更新（如果 roomInfo 中有 players）
    if (gameStore.currentRoomInfo.roomInfo?.players) {
      const roomInfo = gameStore.currentRoomInfo.roomInfo
      console.log('[GameView] Processing existing players from roomInfo:', roomInfo.players)

      // 调用 handleRoomInfo 处理玩家信息
      gameHandlers.handleRoomInfo({
        type: 'room_info',
        data: roomInfo
      })
    } else {
      console.log('[GameView] No players in existing roomInfo')
    }
  }

  if (accessToken && userId && roomId.value) {
    try {
      // 检查是否已经连接并认证（从 LobbyView 复用）
      if (wsConnected.value && wsAuthenticated.value) {
        // v2.1.0: 已连接，只需发送 enter_room 消息
        addSystemMessage('已连接到游戏服务器')
        enterRoom(roomId.value)

        // 检查是否已在房间中（从 LobbyView 跳转过来的情况）
        if (gameStore.currentRoomInfo?.id === roomId.value) {
          // 已通过 WebSocket 创建/加入房间
          addSystemMessage('欢迎来到游戏房间')

          // v2.4.7: 检查玩家信息是否完整，如果不完整则重新处理
          if (!players.value.black && !players.value.white) {
            console.log('[GameView] Player info missing on mount, reprocessing room info...')
            if (gameStore.currentRoomInfo.roomInfo) {
              gameHandlers.handleRoomInfo({
                type: 'room_info',
                data: gameStore.currentRoomInfo.roomInfo
              })
            }
          }
        } else {
          // 需要加入房间（可能是直接访问 URL 或刷新页面）
          joinRoomWS(roomId.value)
          addSystemMessage('正在加入房间...')
        }
      } else {
        // v2.1.0: 未连接，建立新连接（兼容直接访问 URL 的情况）
        addSystemMessage('正在连接游戏服务器...')

        // 先获取房间详情，检查当前玩家是否已在房间中
        const { gomokuApi } = await import('@/api/gomoku.api')
        const roomDetail = await gomokuApi.getRoomDetail(roomId.value)
        const isInRoom = roomDetail.players.some((p: { id: string }) => p.id === userId)

        if (isInRoom) {
          // 玩家已在房间中，只建立连接和认证
          addSystemMessage('欢迎回到游戏房间')
          await connect(accessToken)

          // 等待 welcome 消息后认证
          wsOn('welcome', () => {
            authenticate(accessToken, userId)
            wsOff('welcome')
          })

          // 等待认证成功后发送 enter_room
          wsOn('auth_success', () => {
            enterRoom(roomId.value)
            wsOff('auth_success')
          })
        } else {
          // 玩家不在房间中，通过 WebSocket 加入
          wsJoinRoom(roomId.value, accessToken, userId)
          addSystemMessage('欢迎来到游戏房间')
        }
      }
    } catch (error) {
      console.error('[GameView] WebSocket connection failed:', error)
      addSystemMessage('连接游戏服务器失败，请刷新页面重试')
    }
  } else {
    addSystemMessage('等待玩家加入...')
    if (!accessToken) {
      ElMessage.warning('请先登录')
      router.push('/login')
    }
  }
})

onUnmounted(() => {
  console.log('[GameView] Component unmounted')
  console.log('[GameView] Final state:', {
    players: players.value,
    totalMoves: totalMoves.value,
    blackTime: blackTime.value,
    whiteTime: whiteTime.value,
    gameConfig: gameConfig.value,
  })

  // 清理游戏时长计时
  gameStartTime.value = null
  
  if (timer) {
    clearInterval(timer)
  }

  // 移除 WebSocket 事件监听
  wsOff('game_started')
  wsOff('ready_confirm')
  wsOff('all_players_ready')
  wsOff('turn_changed')
  wsOff('piece_placed')
  wsOff('move_result')           // v2.3.0
  wsOff('game_state_update')     // v2.3.0
  wsOff('game_end')
  wsOff('player_joined')
  wsOff('player_left')
  wsOff('chat')
  wsOff('player_surrendered')    // v2.4.0
  wsOff('draw_offer')            // v2.4.0
  wsOff('draw_response')         // v2.4.0
  wsOff('undo_request')          // v2.4.0
  wsOff('move_undone')           // v2.4.0
  wsOff('time_update')           // v2.4.0
  wsOff('connect')
  wsOff('disconnect')
  wsOff('auth_success')
  wsOff('game_state')           // v2.3.0
  wsOff('game_settlement')      // v2.6.0

  // v2.1.0: 不在这里断开连接，保持连接供大厅复用
  // 如果用户通过其他方式离开（如关闭页面、刷新），浏览器会自动断开
  // 只有在返回首页时才断开连接（由 LobbyView 的 goBack 处理）

  // 重置游戏状态（但不断开 WebSocket）
  gameStore.resetGame()
})
</script>

<style scoped>
.game-page {
  min-height: 100vh;
  background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
  padding: 1rem;
}

/* 头部 */
.game-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
  padding: 1rem 1.5rem;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  border: 1px solid rgba(255, 255, 255, 0.1);
}

.room-info {
  display: flex;
  align-items: center;
  gap: 1.5rem;
}

.back-btn {
  color: rgba(255, 255, 255, 0.8);
}

.back-btn:hover {
  color: white;
}

.room-title {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.room-title h1 {
  font-size: 1.25rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.header-actions {
  display: flex;
  gap: 0.75rem;
}

/* 主内容 */
.game-content {
  display: grid;
  grid-template-columns: 280px 1fr 280px;
  gap: 1.5rem;
  max-width: 1400px;
  margin: 0 auto;
}

/* 侧边面板 */
.side-panel {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

/* 玩家卡片 */
.player-card {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 0.875rem 1rem;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  border: 2px solid transparent;
  transition: all 0.3s ease;
  min-width: 0;
}

.player-card.active {
  border-color: #667eea;
  background: rgba(102, 126, 234, 0.15);
  box-shadow: 0 0 20px rgba(102, 126, 234, 0.3);
}

.player-card.winner {
  border-color: #f59e0b;
  background: rgba(245, 158, 11, 0.15);
}

.player-avatar {
  width: 48px;
  height: 48px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 700;
  font-size: 1.125rem;
  flex-shrink: 0;
}

.player-avatar.black-piece {
  background: radial-gradient(circle at 30% 30%, #4a4a4a 30%, #1a1a1a 100%);
  color: white;
  border: 2px solid #333;
  box-shadow: inset 0 2px 4px rgba(255, 255, 255, 0.1);
}

.player-avatar.white-piece {
  background: radial-gradient(circle at 30% 30%, #fff 30%, #ddd 100%);
  color: #333;
  border: 2px solid #ccc;
  box-shadow: inset 0 2px 4px rgba(0, 0, 0, 0.1);
}

.player-details {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  justify-content: center;
  gap: 0.25rem;
}

.player-name {
  font-weight: 600;
  color: white;
  font-size: 0.9rem;
  display: flex;
  align-items: center;
  gap: 0.5rem;
  flex-wrap: wrap;
}

.player-name span {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 180px;
}

.player-stats {
  display: flex;
  gap: 0.75rem;
  font-size: 0.75rem;
  color: rgba(255, 255, 255, 0.7);
  flex-wrap: wrap;
}

.player-stats .rating {
  display: flex;
  align-items: center;
  gap: 0.25rem;
  color: #f59e0b;
  font-weight: 500;
}

.player-stats .captures {
  display: flex;
  align-items: center;
  gap: 0.25rem;
}

.time-display {
  font-size: 1.125rem;
  font-weight: 700;
  color: white;
  font-family: 'Courier New', 'Consolas', monospace;
  flex-shrink: 0;
  min-width: 80px;
  text-align: center;
  padding: 0.25rem 0.5rem;
  background: rgba(0, 0, 0, 0.2);
  border-radius: 8px;
}

.time-display.danger {
  color: #ef4444;
  background: rgba(239, 68, 68, 0.15);
  animation: pulse 1s infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

/* 游戏信息卡片 */
.game-info-card {
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  padding: 1rem;
}

.game-info-card h3 {
  color: white;
  font-size: 0.875rem;
  margin-bottom: 0.75rem;
  padding-bottom: 0.5rem;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.info-item {
  display: flex;
  justify-content: space-between;
  padding: 0.5rem 0;
  font-size: 0.875rem;
}

.info-item .label {
  color: rgba(255, 255, 255, 0.6);
}

.info-item .value {
  color: white;
  font-weight: 500;
}

/* 操作按钮 */
.action-buttons {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.action-buttons .el-button {
  justify-content: flex-start;
}

/* 棋盘容器 */
.board-container {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 1rem;
}

.turn-indicator {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 0.75rem 1.5rem;
  background: rgba(255, 255, 255, 0.1);
  border-radius: 100px;
  color: white;
  font-size: 0.875rem;
}

.turn-piece {
  width: 20px;
  height: 20px;
  border-radius: 50%;
}

.turn-piece.black {
  background: radial-gradient(circle, #4a4a4a 30%, #1a1a1a 100%);
}

.turn-piece.white {
  background: radial-gradient(circle, #fff 30%, #ddd 100%);
  border: 1px solid #ccc;
}

/* 聊天区域 */
.chat-section {
  flex: 1;
  display: flex;
  flex-direction: column;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  overflow: hidden;
}

.chat-header {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.75rem 1rem;
  background: rgba(255, 255, 255, 0.05);
  color: white;
  font-size: 0.875rem;
  font-weight: 500;
}

.chat-messages {
  flex: 1;
  padding: 0.75rem;
  overflow-y: auto;
  max-height: 200px;
}

.chat-message {
  margin-bottom: 0.5rem;
  font-size: 0.8rem;
}

.chat-message .sender {
  color: #667eea;
  font-weight: 500;
  margin-right: 0.25rem;
}

.chat-message .content {
  color: rgba(255, 255, 255, 0.8);
}

.chat-message.system .content {
  color: rgba(255, 255, 255, 0.5);
  font-style: italic;
}

.chat-message.own .sender {
  color: #f59e0b;
}

.chat-input {
  padding: 0.75rem;
  border-top: 1px solid rgba(255, 255, 255, 0.1);
}

.chat-input :deep(.el-input__wrapper) {
  background: rgba(255, 255, 255, 0.1);
  border: none;
}

/* 落子历史 */
.move-history {
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  overflow: hidden;
}

.history-header {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.75rem 1rem;
  background: rgba(255, 255, 255, 0.05);
  color: white;
  font-size: 0.875rem;
  font-weight: 500;
}

.history-list {
  padding: 0.5rem 1rem;
  max-height: 150px;
  overflow-y: auto;
}

.history-item {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.25rem 0;
  font-size: 0.8rem;
}

.move-number {
  color: rgba(255, 255, 255, 0.5);
  width: 30px;
}

.move-color {
  width: 12px;
  height: 12px;
  border-radius: 50%;
}

.move-color.black {
  background: #1a1a1a;
}

.move-color.white {
  background: white;
  border: 1px solid #ccc;
}

.move-position {
  color: white;
}

/* 游戏结果弹窗 */
.result-content {
  text-align: center;
  padding: 1rem;
}

.result-icon {
  width: 80px;
  height: 80px;
  margin: 0 auto 1rem;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 2.5rem;
}

.result-icon.win {
  background: linear-gradient(135deg, #fbbf24 0%, #f59e0b 100%);
  color: white;
}

.result-icon.lose {
  background: linear-gradient(135deg, #f87171 0%, #ef4444 100%);
  color: white;
}

.result-icon.draw {
  background: linear-gradient(135deg, #9ca3af 0%, #6b7280 100%);
  color: white;
}

.result-message {
  font-size: 1.1rem;
  color: #374151;
  margin-bottom: 1.5rem;
}

.result-stats {
  display: flex;
  justify-content: center;
  gap: 3rem;
}

.result-stats .stat {
  display: flex;
  flex-direction: column;
}

.result-stats .value {
  font-size: 1.5rem;
  font-weight: 700;
  color: #1a1a2e;
}

.result-stats .label {
  font-size: 0.875rem;
  color: #6b7280;
}

/* 游戏结算奖励样式 */
.result-rewards {
  margin-top: 1.5rem;
  padding-top: 1rem;
  border-top: 1px solid #e5e7eb;
}

.result-rewards h4,
.result-leaderboard h4,
.result-achievements h4 {
  font-size: 0.875rem;
  color: #6b7280;
  margin-bottom: 0.75rem;
}

.rewards-list {
  display: flex;
  justify-content: center;
  gap: 1.5rem;
  flex-wrap: wrap;
}

.reward-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 0.25rem;
  padding: 0.75rem 1rem;
  background: #f9fafb;
  border-radius: 8px;
}

.reward-icon {
  font-size: 1.5rem;
}

.reward-icon.gold {
  color: #fbbf24;
}

.reward-icon.gems {
  color: #a855f7;
}

.reward-icon.exp {
  color: #3b82f6;
}

.reward-icon.honor {
  color: #10b981;
}

.reward-value {
  font-size: 1.125rem;
  font-weight: 700;
  color: #374151;
}

.reward-label {
  font-size: 0.75rem;
  color: #9ca3af;
}

/* 排行榜变化样式 */
.result-leaderboard {
  margin-top: 1rem;
  padding-top: 1rem;
  border-top: 1px solid #e5e7eb;
}

.leaderboard-change {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.75rem;
}

.rank-old,
.rank-new {
  font-size: 1.25rem;
  font-weight: 600;
}

.rank-old {
  color: #9ca3af;
}

.rank-new {
  color: #10b981;
}

.rank-arrow {
  color: #9ca3af;
  font-size: 1rem;
}

.rank-arrow.up {
  color: #10b981;
  transform: rotate(-90deg);
}

.rank-improvement {
  font-size: 0.75rem;
  color: #10b981;
  font-weight: 500;
}

/* 成就展示样式 */
.result-achievements {
  margin-top: 1rem;
  padding-top: 1rem;
  border-top: 1px solid #e5e7eb;
}

.achievements-list {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.achievement-item {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 0.5rem 0.75rem;
  background: #f9fafb;
  border-radius: 8px;
}

.achievement-icon {
  font-size: 1.25rem;
  color: #fbbf24;
}

.achievement-info {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  flex: 1;
}

.achievement-name {
  font-weight: 500;
  color: #374151;
}

/* 响应式 */
@media (max-width: 1200px) {
  .game-content {
    grid-template-columns: 1fr;
    gap: 1rem;
  }

  .side-panel {
    flex-direction: row;
    flex-wrap: wrap;
  }

  .side-panel > * {
    flex: 1;
    min-width: 250px;
  }
}

@media (max-width: 768px) {
  .game-header {
    flex-direction: column;
    gap: 1rem;
  }

  .room-info {
    flex-direction: column;
    gap: 0.5rem;
    text-align: center;
  }

  .side-panel {
    flex-direction: column;
  }

  .side-panel > * {
    min-width: auto;
  }

  .player-card {
    flex-direction: column;
    align-items: flex-start;
    gap: 0.5rem;
    padding: 0.75rem;
  }

  .player-avatar {
    align-self: center;
  }

  .time-display {
    align-self: center;
    min-width: 100px;
  }

  .player-name span {
    max-width: 100%;
  }
}

@media (max-width: 480px) {
  .player-card {
    padding: 0.625rem;
  }

  .player-avatar {
    width: 40px;
    height: 40px;
    font-size: 1rem;
  }

  .player-name {
    font-size: 0.85rem;
  }

  .player-name span {
    max-width: 100%;
  }

  .player-stats {
    font-size: 0.7rem;
  }

  .time-display {
    font-size: 1rem;
    min-width: 90px;
    padding: 0.2rem 0.4rem;
  }
}
</style>
