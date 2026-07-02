<template>
  <div class="lobby-page">
    <!-- 顶部游戏信息栏 -->
    <div class="game-header-bar">
      <el-button text @click="goBack" class="back-btn">
        <el-icon><ArrowLeft /></el-icon>
        返回游戏选择
      </el-button>
      <div class="game-title">
        <span class="game-icon">{{ gameInfo?.icon || '🎮' }}</span>
        <h1>{{ gameInfo?.displayName || '游戏大厅' }}</h1>
        <el-tag v-if="gameInfo" size="small" effect="plain">{{ gameInfo.category }}</el-tag>
      </div>
      <!-- v2.1.0 新增：连接状态指示器 -->
      <div class="connection-status">
        <el-tooltip :content="isConnected ? (isAuthenticated ? '已连接并认证' : '已连接，等待认证') : '未连接'">
          <div class="status-indicator" :class="{ connected: isConnected, authenticated: isAuthenticated }">
            <el-icon><Connection /></el-icon>
            <span>{{ isConnected ? (isAuthenticated ? '已连接' : '连接中') : '离线' }}</span>
          </div>
        </el-tooltip>
      </div>
    </div>

    <!-- 顶部统计信息 -->
    <div class="stats-bar">
      <div class="stat-item">
        <div class="stat-icon">
          <el-icon><User /></el-icon>
        </div>
        <div class="stat-info">
          <span class="stat-value">{{ onlinePlayers }}</span>
          <span class="stat-label">在线玩家</span>
        </div>
      </div>
      <div class="stat-item">
        <div class="stat-icon">
          <el-icon><House /></el-icon>
        </div>
        <div class="stat-info">
          <span class="stat-value">{{ activeRooms }}</span>
          <span class="stat-label">活跃房间</span>
        </div>
      </div>
      <div class="stat-item">
        <div class="stat-icon">
          <el-icon><TrophyBase /></el-icon>
        </div>
        <div class="stat-info">
          <span class="stat-value">{{ totalGames }}</span>
          <span class="stat-label">今日对局</span>
        </div>
      </div>
    </div>

    <!-- 主要内容区 -->
    <div class="lobby-container">
      <!-- 左侧：快捷操作 -->
      <div class="quick-actions">
        <div class="action-card create-room" @click="showCreateDialog = true">
          <div class="action-icon">
            <el-icon><Plus /></el-icon>
          </div>
          <div class="action-text">
            <h3>创建房间</h3>
            <p>邀请好友一起游戏</p>
          </div>
        </div>
        <div class="action-card quick-match" @click="handleQuickMatch">
          <div class="action-icon">
            <el-icon><Lightning /></el-icon>
          </div>
          <div class="action-text">
            <h3>快速匹配</h3>
            <p>自动匹配实力相当的对手</p>
          </div>
        </div>
      </div>

      <!-- 右侧：房间列表 -->
      <div class="room-section">
        <div class="section-header">
          <h2>
            <el-icon><List /></el-icon>
            房间列表
            <!-- v2.1.0 新增：实时更新标签 -->
            <el-tag v-if="isRoomListSubscribed" size="small" type="success" effect="light" class="live-tag">
              实时更新
            </el-tag>
          </h2>
          <el-button text @click="handleRefreshRooms" :loading="loading">
            <el-icon><Refresh /></el-icon>
            刷新
          </el-button>
        </div>

        <div class="room-list" v-loading="loading">
          <TransitionGroup name="room-list">
            <div
              v-for="room in rooms"
              :key="room.id"
              class="room-card"
              :class="{ disabled: room.status !== 'waiting' || room.players.length >= room.maxPlayers }"
              @click="handleJoinRoom(room.id)"
            >
              <div class="room-header">
                <h3 class="room-name">{{ room.name }}</h3>
                <el-tag
                  :type="getStatusType(room.status)"
                  size="small"
                  effect="dark"
                >
                  {{ getStatusText(room.status) }}
                </el-tag>
              </div>

              <div class="room-body">
                <div class="room-host">
                  <el-avatar :size="32" class="host-avatar">
                    {{ room.host?.username?.charAt(0)?.toUpperCase() || '?' }}
                  </el-avatar>
                  <div class="host-info">
                    <span class="host-name">{{ room.host?.username || '未知' }}</span>
                    <span class="host-rating">
                      <el-icon><Star /></el-icon>
                      {{ room.host?.rating || 1000 }}
                    </span>
                  </div>
                </div>

                <div class="room-players">
                  <div class="player-slots">
                    <div
                      v-for="i in room.maxPlayers"
                      :key="i"
                      class="slot"
                      :class="{ filled: i <= room.players.length }"
                    >
                      <el-icon v-if="i <= room.players.length"><UserFilled /></el-icon>
                      <el-icon v-else><User /></el-icon>
                    </div>
                  </div>
                  <span class="player-count">{{ room.players.length }}/{{ room.maxPlayers }}</span>
                </div>
              </div>

              <div class="room-footer">
                <div class="room-game-type">
                  <el-icon><Grid /></el-icon>
                  {{ room.gameType || '五子棋' }}
                </div>
                <el-button
                  type="primary"
                  size="small"
                  :disabled="room.status !== 'waiting' || room.players.length >= room.maxPlayers"
                  @click.stop="handleJoinRoom(room.id)"
                >
                  加入房间
                </el-button>
              </div>
            </div>
          </TransitionGroup>

          <el-empty v-if="!loading && rooms.length === 0" description="暂无房间，快来创建一个吧！">
            <el-button type="primary" @click="showCreateDialog = true">创建房间</el-button>
          </el-empty>
        </div>
      </div>
    </div>

    <!-- 创建房间对话框 -->
    <el-dialog
      v-model="showCreateDialog"
      title="创建房间"
      width="480px"
      class="create-room-dialog"
      :close-on-click-modal="false"
    >
      <el-form :model="createForm" label-position="top" class="create-form">
        <el-form-item label="房间名称">
          <el-input
            v-model="createForm.name"
            placeholder="给您的房间起个名字"
            maxlength="20"
            show-word-limit
          />
        </el-form-item>

        <el-form-item label="游戏模式">
          <el-radio-group v-model="createForm.gameMode" class="game-mode-group">
            <el-radio-button value="freestyle">自由模式</el-radio-button>
            <el-radio-button value="renju">连珠模式</el-radio-button>
            <el-radio-button value="swap2">Swap2</el-radio-button>
          </el-radio-group>
        </el-form-item>

        <el-form-item label="思考时间">
          <el-select v-model="createForm.timeLimit" style="width: 100%">
            <el-option label="5 分钟（快棋）" :value="300" />
            <el-option label="10 分钟（标准）" :value="600" />
            <el-option label="15 分钟（慢棋）" :value="900" />
            <el-option label="30 分钟（长考）" :value="1800" />
          </el-select>
        </el-form-item>

        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="允许悔棋">
              <el-switch v-model="createForm.allowUndo" />
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="允许观战">
              <el-switch v-model="createForm.allowSpectators" />
            </el-form-item>
          </el-col>
        </el-row>
      </el-form>

      <template #footer>
        <el-button @click="showCreateDialog = false">取消</el-button>
        <el-button type="primary" @click="handleCreateRoom" :loading="loading">
          创建并进入
        </el-button>
      </template>
    </el-dialog>

    <!-- v2.6.0: 匹配面板 -->
    <MatchingPanel
      v-model="showMatchPanel"
      :game-type="gameType"
      @matched="handleMatchFound"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted, onUnmounted } from 'vue'
import { storeToRefs } from 'pinia'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import {
  User,
  House,
  TrophyBase,
  Plus,
  Lightning,
  List,
  Refresh,
  Star,
  UserFilled,
  Grid,
  ArrowLeft,
  Connection,
} from '@element-plus/icons-vue'
import { useGameStore, useAuthStore, useGameDataStore, useMatchStore } from '@/stores'
import { getGameServicesForDisplay, serviceApi } from '@/api/service.api'
import { getGameTypeInfo, type GameTypeInfo } from '@/types/service.types'
import { useGomokuSocket } from '@/websocket'
import { gameHandlers } from '@/websocket/handlers/game.handler'
import MatchingPanel from '@/components/game/MatchingPanel.vue'

const route = useRoute()
const router = useRouter()
const gameStore = useGameStore()
const authStore = useAuthStore()
const gameDataStore = useGameDataStore()
const matchStore = useMatchStore()

// v2.1.0: WebSocket 连接
const {
  isConnected,
  isAuthenticated,
  isRoomListSubscribed,
  connect,
  disconnect,
  authenticate,
  subscribeRoomList,
  unsubscribeRoomList,
  createRoom: createRoomWS,
  joinRoomWS,
  startMatch,
  on: wsOn,
  off: wsOff,
} = useGomokuSocket()

// 从路由获取游戏类型
const gameType = computed(() => route.params.gameType as string || 'gomoku')

// 获取当前游戏信息
const gameInfo = computed((): GameTypeInfo | undefined => {
  return getGameTypeInfo(gameType.value)
})

// 使用 storeToRefs 解构响应式状态，保持响应式
const { rooms, loading } = storeToRefs(gameStore)
const { getRooms } = gameStore

// 统计数据（从服务注册中心获取）
const onlinePlayers = ref(0)
const activeRooms = ref(0)
const totalGames = ref(0)

const showCreateDialog = ref(false)
// v2.6.0: 匹配面板状态
const showMatchPanel = ref(false)
// v2.1.0: 追踪房间创建状态（用于从 room_added 消息获取房间 ID）
const isWaitingForRoomCreation = ref(false)
// v2.5.6: 追踪房间加入状态（等待 room_joined 响应后再跳转）
const isWaitingForRoomJoin = ref(false)
const pendingJoinRoomId = ref<string | null>(null)
let roomCreationTimeout: ReturnType<typeof setTimeout> | null = null
let roomJoinTimeout: ReturnType<typeof setTimeout> | null = null
const createForm = reactive({
  name: '',
  gameMode: 'freestyle',
  timeLimit: 900,
  allowUndo: false,
  allowSpectators: true,
})

const getStatusType = (status: string) => {
  const types: Record<string, string> = {
    waiting: 'success',
    playing: 'warning',
    ended: 'info',
  }
  return types[status] || 'info'
}

const getStatusText = (status: string) => {
  const statusMap: Record<string, string> = {
    waiting: '等待中',
    playing: '游戏中',
    ended: '已结束',
  }
  return statusMap[status] || status
}

// 加载游戏服务统计
const loadServiceStats = async () => {
  try {
    const response = await getGameServicesForDisplay({
      game_type: gameType.value,
      healthy_only: true
    })

    const services = response.services || []
    if (services.length > 0) {
      onlinePlayers.value = services.reduce((sum, s) => sum + s.onlinePlayers, 0)
      activeRooms.value = services.reduce((sum, s) => sum + s.activeRooms, 0)
    }
  } catch {
    onlinePlayers.value = 0
    activeRooms.value = 0
  }
}

// 返回游戏选择页
const goBack = () => {
  // v2.1.0: 返回时取消订阅并断开连接
  unsubscribeRoomList()
  disconnect()
  gameStore.setWsConnected(false)
  gameStore.setWsSubscribed(false)
  router.push('/')
}

// v2.1.0: 手动刷新房间列表（通过 WebSocket）
const handleRefreshRooms = () => {
  if (isAuthenticated.value) {
    subscribeRoomList(gameType.value)
  } else {
    // 回退到 HTTP API
    getRooms()
  }
}

const handleJoinRoom = async (roomId: string) => {
  try {
    const user = authStore.user
    if (!user) {
      ElMessage.warning('请先登录')
      router.push('/login')
      return
    }

    // v2.1.0: 通过 WebSocket 加入房间
    if (isAuthenticated.value) {
      // v2.5.6: 设置等待标志，等待 room_joined 响应后再跳转
      isWaitingForRoomJoin.value = true
      pendingJoinRoomId.value = roomId

      // 清除之前的超时
      if (roomJoinTimeout) {
        clearTimeout(roomJoinTimeout)
        roomJoinTimeout = null
      }

      // 设置当前房间信息（避免 GameView 重复发送 join_room）
      gameStore.setCurrentRoom({
        id: roomId,
        isCreator: false,
      })

      // 已连接，直接通过 WebSocket 加入
      joinRoomWS(roomId)

      // 设置超时（5秒内没有收到 room_joined 则直接跳转）
      roomJoinTimeout = setTimeout(() => {
        if (isWaitingForRoomJoin.value && pendingJoinRoomId.value === roomId) {
          console.log('[LobbyView] room_joined timeout, navigating anyway')
          isWaitingForRoomJoin.value = false
          pendingJoinRoomId.value = null
          router.push(`/game/${gameType.value}/room/${roomId}`)
        }
      }, 5000)
    } else {
      // 未连接，回退到直接跳转（GameView 会处理连接）
      router.push(`/game/${gameType.value}/room/${roomId}`)
    }
  } catch (error: unknown) {
    const errorMessage = error instanceof Error ? error.message : '加入房间失败'
    ElMessage.error(errorMessage)
  }
}

const handleCreateRoom = async () => {
  try {
    const user = authStore.user
    if (!user) {
      ElMessage.warning('请先登录')
      router.push('/login')
      return
    }

    // v2.1.0: 优先使用 WebSocket 创建房间
    if (isAuthenticated.value) {
      // 设置等待标志，用于从 room_added 消息中识别并跳转
      isWaitingForRoomCreation.value = true

      // 清除之前的超时
      if (roomCreationTimeout) {
        clearTimeout(roomCreationTimeout)
      }

      // 设置 5 秒超时，如果没有收到有效的房间 ID，尝试从房间列表查找
      roomCreationTimeout = setTimeout(() => {
        if (isWaitingForRoomCreation.value) {
          console.warn('[LobbyView] Timeout waiting for room ID, checking room list...')
          isWaitingForRoomCreation.value = false

          // 尝试从房间列表中找到最近创建的房间（创建者是当前用户）
          const currentUserId = authStore.user?.id
          console.log('[LobbyView] Looking for room in list, currentUserId:', currentUserId)

          const myRoom = rooms.value.find(room =>
            room.host?.id === currentUserId && room.status === 'waiting'
          )

          if (myRoom) {
            const targetPath = `/game/${gameType.value}/room/${myRoom.id}`
            console.log('[LobbyView] Found room from list, navigating to:', targetPath)
            router.push(targetPath)
          } else {
            ElMessage.warning('房间创建成功，请刷新页面查看')
          }
        }
      }, 5000)

      createRoomWS(user.id, {
        gameMode: createForm.gameMode,
        timeLimit: createForm.timeLimit,
        incrementPerMove: 10,
        allowUndo: createForm.allowUndo,
        allowSpectators: createForm.allowSpectators,
        maxSpectators: 50,
      })
      showCreateDialog.value = false
      // 注意：跳转会在收到 room_created（有 roomId）或 room_added 消息后处理
    } else {
      // 回退到 HTTP API
      const result = await gameStore.createRoom({
        creatorId: user.id,
        config: {
          gameMode: createForm.gameMode as 'freestyle' | 'renju' | 'swap2',
          timeLimit: createForm.timeLimit,
          allowUndo: createForm.allowUndo,
          allowSpectators: createForm.allowSpectators,
        },
      })

      showCreateDialog.value = false
      resetCreateForm()

      if (result.roomId || result.id) {
        router.push(`/game/${gameType.value}/room/${result.roomId || result.id}`)
      }
    }
  } catch (error: unknown) {
    const errorMessage = error instanceof Error ? error.message : '创建房间失败'
    ElMessage.error(errorMessage)
  }
}

const handleQuickMatch = async () => {
  // v2.6.0: 使用 WebSocket 匹配
  if (!isAuthenticated.value) {
    ElMessage.warning('请先完成认证')
    return
  }

  // 开始匹配
  startMatch(gameType.value)
  matchStore.startMatching()
  showMatchPanel.value = true

  console.log('[LobbyView] Quick match started for:', gameType.value)
}

/**
 * v2.6.0: 处理匹配成功
 * 匹配成功后跳转到房间
 */
const handleMatchFound = (roomId: string) => {
  console.log('[LobbyView] Match found, navigating to room:', roomId)
  matchStore.clearMatchResult()
  showMatchPanel.value = false
  router.push(`/game/${gameType.value}/room/${roomId}`)
}

const resetCreateForm = () => {
  createForm.name = ''
  createForm.gameMode = 'freestyle'
  createForm.timeLimit = 900
  createForm.allowUndo = false
  createForm.allowSpectators = true
}

// 定期更新统计数据（不再轮询房间列表）
let statsInterval: ReturnType<typeof setInterval>

/**
 * v2.1.0: 建立 WebSocket 连接
 * 在选择游戏（进入五子棋大厅）时建立连接
 */
const establishWebSocketConnection = async () => {
  const accessToken = authStore.accessToken
  const userId = authStore.user?.id

  if (!accessToken || !userId) {
    console.warn('[LobbyView] No auth token, skipping WebSocket connection')
    // 回退到 HTTP API
    await getRooms()
    return
  }

  try {
    // 1. 连接 WebSocket
    await connect(accessToken)
    gameStore.setWsConnected(true)
    console.log('[LobbyView] WebSocket connected')

    // 2. 认证
    authenticate(accessToken, userId)

    // 3. 订阅房间列表（在认证成功后）
    // 注意：订阅在 handleAuthSuccess 回调中执行
  } catch (error) {
    console.error('[LobbyView] WebSocket connection failed:', error)
    // 回退到 HTTP API
    await getRooms()
    gameStore.setWsConnected(false)
  }
}

/**
 * 加载游戏数据（根据文档 v2.0.0，在选择游戏时加载）
 */
const loadGameData = async () => {
  const userId = authStore.user?.id
  if (!userId) return

  try {
    const displayName = authStore.user?.username || undefined

    await Promise.all([
      serviceApi.getRecommendedGameServer(gameType.value).then(response => {
        if (response?.recommended) {
          const wsPort = response.recommended.metadata?.websocket_port
            ? parseInt(response.recommended.metadata.websocket_port, 10)
            : response.recommended.port + 1
          authStore.setGameServer({
            gameType: gameType.value,
            host: response.recommended.host,
            port: response.recommended.port,
            wsPort,
          })
          console.log('[LobbyView] Game server loaded:', response.recommended.service_name)
        }
      }).catch(e => {
        console.warn('[LobbyView] Failed to load game server:', e)
      }),

      gameDataStore.loadAllGameData(userId, displayName, gameType.value).catch(e => {
        console.warn('[LobbyView] Failed to load game data:', e)
      }),
    ])

    console.log('[LobbyView] Game data loaded for:', gameType.value)
  } catch (error) {
    console.error('[LobbyView] Failed to load game data:', error)
  }
}

// v2.1.0: WebSocket 消息处理回调
const handleAuthSuccess = () => {
  console.log('[LobbyView] Auth success, subscribing to room list')
  // 认证成功后订阅房间列表
  subscribeRoomList(gameType.value)
}

const handleRoomCreated = (message: any) => {
  console.log('[LobbyView] Room created - full message:', JSON.stringify(message, null, 2))

  // 兼容两种字段名：roomId (camelCase) 和 room_id (snake_case)
  const roomId = message.data.roomId || message.data.room_id

  // 使用 gameHandler 处理（更新 pieceType 等信息）
  gameHandlers.handleRoomCreated(message)

  if (roomId) {
    // 有 roomId，直接跳转
    console.log('[LobbyView] Room created with ID:', roomId)
    isWaitingForRoomCreation.value = false
    router.push(`/game/${gameType.value}/room/${roomId}`)
  } else {
    // roomId 为空（服务端 bug），等待 room_added 消息获取房间 ID
    console.warn('[LobbyView] Room created but roomId is empty, waiting for room_added...')
    // isWaitingForRoomCreation 已经在 handleCreateRoom 中设置为 true
    // 超时处理会在 handleCreateRoomSuccess 中触发
  }
}

// v2.1.0: 处理服务端返回的 create_room_success（实际返回格式）
const handleCreateRoomSuccess = (message: any) => {
  console.log('[LobbyView] Create room success:', message.data)

  // 如果响应中包含 roomId，直接跳转
  if (message.data.roomId) {
    isWaitingForRoomCreation.value = false
    console.log('[LobbyView] Room ID from response:', message.data.roomId)
    router.push(`/game/${gameType.value}/room/${message.data.roomId}`)
    return
  }

  // 服务端只返回 playerId，设置超时等待 room_added 消息
  console.log('[LobbyView] Waiting for room_added message...')

  // 清除之前的超时
  if (roomCreationTimeout) {
    clearTimeout(roomCreationTimeout)
  }

  // 设置 5 秒超时，如果没有收到 room_added，尝试从房间列表查找
  roomCreationTimeout = setTimeout(() => {
    if (isWaitingForRoomCreation.value) {
      console.warn('[LobbyView] Timeout waiting for room_added, checking room list...')
      isWaitingForRoomCreation.value = false

      // 尝试从房间列表中找到最近创建的房间（创建者是当前用户）
      const currentUserId = authStore.user?.id
      console.log('[LobbyView] Looking for room in list, currentUserId:', currentUserId)
      console.log('[LobbyView] Rooms:', JSON.stringify(rooms.value, null, 2))

      const myRoom = rooms.value.find(room =>
        room.host?.id === currentUserId && room.status === 'waiting'
      )

      if (myRoom) {
        const targetPath = `/game/${gameType.value}/room/${myRoom.id}`
        console.log('[LobbyView] Found room from list, navigating to:', targetPath)
        router.push(targetPath)
      } else {
        ElMessage.warning('房间创建成功，但无法获取房间信息，请刷新页面')
      }
    }
  }, 5000)
}

// v2.1.0: 处理 room_added 消息，用于房间创建后的导航
const handleRoomAddedForNavigation = (message: any) => {
  console.log('[LobbyView] Room added - full message:', JSON.stringify(message, null, 2))

  // 使用 gameHandler 处理房间列表更新
  gameHandlers.handleRoomAdded(message)

  // 如果是等待房间创建响应，检查是否是自己创建的房间并跳转
  if (isWaitingForRoomCreation.value) {
    const room = message.data?.room
    const currentUserId = authStore.user?.id

    // v2.5.4: 兼容 camelCase 和 snake_case
    const creatorId = room?.creatorId || room?.creator_id
    const roomId = room?.roomId || room?.room_id

    console.log('[LobbyView] Checking room:', {
      room: room,
      roomCreatorId: creatorId,
      currentUserId,
      gameType: gameType.value,
      isWaiting: isWaitingForRoomCreation.value
    })

    // 检查房间创建者是否是当前用户
    if (room && creatorId === currentUserId) {
      // 清除超时
      if (roomCreationTimeout) {
        clearTimeout(roomCreationTimeout)
        roomCreationTimeout = null
      }
      isWaitingForRoomCreation.value = false

      const targetPath = `/game/${gameType.value}/room/${roomId}`
      console.log('[LobbyView] Navigating to:', targetPath)
      router.push(targetPath)
    } else {
      console.warn('[LobbyView] Room creator does not match current user, skipping navigation')
    }
  }
}

onMounted(async () => {
  // 加载统计数据
  loadServiceStats()

  // 加载游戏数据
  await loadGameData()

  // v2.1.0: 建立 WebSocket 连接
  await establishWebSocketConnection()

  // 注册 WebSocket 事件监听
  wsOn('auth_success', handleAuthSuccess)
  // v2.1.0: 监听两种房间创建成功消息格式
  wsOn('room_created', handleRoomCreated)
  wsOn('create_room_success', handleCreateRoomSuccess)

  // 使用 gameHandler 处理房间列表更新
  wsOn('room_list', (message: any) => gameHandlers.handleRoomList(message))
  // v2.1.0: room_added 用于房间创建后的导航
  wsOn('room_added', handleRoomAddedForNavigation)
  wsOn('room_updated', (message: any) => gameHandlers.handleRoomUpdated(message))
  wsOn('room_removed', (message: any) => gameHandlers.handleRoomRemoved(message))

  // v2.5.6: 监听 room_joined 消息，等待响应后再跳转到房间页面
  wsOn('room_joined', (message: any) => {
    console.log('[LobbyView] room_joined received:', message.data)

    // 先让 gameHandler 处理消息（更新 store）
    gameHandlers.handleRoomJoined(message)

    // 检查是否是我们等待的加入响应
    if (isWaitingForRoomJoin.value && pendingJoinRoomId.value === message.data?.roomId) {
      console.log('[LobbyView] room_joined matched pending join, navigating to room')

      // 清除超时
      if (roomJoinTimeout) {
        clearTimeout(roomJoinTimeout)
        roomJoinTimeout = null
      }

      isWaitingForRoomJoin.value = false
      pendingJoinRoomId.value = null

      // 跳转到房间页面
      const targetPath = `/game/${gameType.value}/room/${message.data.roomId}`
      console.log('[LobbyView] Navigating to:', targetPath)
      router.push(targetPath)
    }
  })

  // 处理订阅确认（服务端实际返回 subscribe_confirm，包含 rooms 数据）
  wsOn('subscribe_confirm', (message: any) => {
    console.log('[LobbyView] Subscribe confirm received:', message.data)
    // 服务端返回格式: { rooms: [...], subscriptionId: "..." }
    if (message.data.rooms) {
      gameStore.setRooms(message.data.rooms)
      console.log('[LobbyView] Rooms loaded from subscribe_confirm:', message.data.rooms.length)
    }
    if (message.data.subscriptionId) {
      gameStore.setWsSubscribed(true)
    }
  })
  // 同时监听 subscription_confirm（文档定义格式）以保持兼容
  wsOn('subscription_confirm', (message: any) => {
    gameHandlers.handleSubscriptionConfirm(message)
    if (message.data.subscribed) {
      gameStore.setWsSubscribed(true)
    }
  })

  // v2.6.0: 匹配系统消息监听
  wsOn('match_status_update', (message: any) => {
    console.log('[LobbyView] match_status_update received:', message)
    gameHandlers.handleMatchStatusUpdate(message)
  })

  wsOn('match_found', (message: any) => {
    console.log('[LobbyView] match_found received:', message)
    gameHandlers.handleMatchFound(message)
    // 匹配成功，跳转到房间
    if (message.data?.room_id) {
      handleMatchFound(message.data.room_id)
    }
  })

  wsOn('match_timeout', (message: any) => {
    console.log('[LobbyView] match_timeout received:', message)
    gameHandlers.handleMatchTimeout(message)
  })

  // 定期更新统计数据（不再轮询房间列表）
  statsInterval = setInterval(() => {
    loadServiceStats()
  }, 30000)
})

onUnmounted(() => {
  clearInterval(statsInterval)

  // 清除房间创建超时
  if (roomCreationTimeout) {
    clearTimeout(roomCreationTimeout)
    roomCreationTimeout = null
  }

  // v2.5.6: 清除房间加入超时
  if (roomJoinTimeout) {
    clearTimeout(roomJoinTimeout)
    roomJoinTimeout = null
  }

  // v2.1.0: 移除 WebSocket 事件监听
  wsOff('auth_success', handleAuthSuccess)
  wsOff('room_created', handleRoomCreated)
  wsOff('create_room_success', handleCreateRoomSuccess)
  wsOff('room_list')
  wsOff('room_added')
  wsOff('room_updated')
  wsOff('room_removed')
  wsOff('room_joined')  // v2.5.6: 移除 room_joined 监听器
  wsOff('subscribe_confirm')
  wsOff('subscription_confirm')
  wsOff('match_status_update')  // v2.6.0
  wsOff('match_found')          // v2.6.0
  wsOff('match_timeout')        // v2.6.0

  // 注意：不在这里断开连接，因为可能要进入房间复用连接
  // 如果返回首页，由 goBack() 处理断开
})
</script>

<style scoped>
.lobby-page {
  min-height: 100vh;
  background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
  padding: 1.5rem;
}

/* 游戏头部栏 */
.game-header-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 1.5rem;
  padding: 1rem 1.5rem;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  max-width: 1400px;
  margin-left: auto;
  margin-right: auto;
}

.back-btn {
  color: rgba(255, 255, 255, 0.8);
}

.back-btn:hover {
  color: white;
}

.game-title {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.game-title .game-icon {
  font-size: 1.75rem;
}

.game-title h1 {
  font-size: 1.25rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.header-spacer {
  width: 120px;
}

/* v2.1.0 新增：连接状态指示器 */
.connection-status {
  display: flex;
  align-items: center;
}

.status-indicator {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.5rem 1rem;
  border-radius: 20px;
  font-size: 0.875rem;
  color: rgba(255, 255, 255, 0.6);
  background: rgba(255, 255, 255, 0.1);
  transition: all 0.3s ease;
}

.status-indicator.connected {
  color: #fbbf24;
  background: rgba(251, 191, 36, 0.15);
}

.status-indicator.authenticated {
  color: #34d399;
  background: rgba(52, 211, 153, 0.15);
}

.status-indicator .el-icon {
  font-size: 1rem;
}

/* 统计栏 */
.stats-bar {
  display: flex;
  gap: 1.5rem;
  max-width: 1400px;
  margin: 0 auto 2rem;
}

.stat-item {
  flex: 1;
  display: flex;
  align-items: center;
  gap: 1rem;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  padding: 1.25rem 1.5rem;
  border: 1px solid rgba(255, 255, 255, 0.1);
  transition: all 0.3s ease;
}

.stat-item:hover {
  background: rgba(255, 255, 255, 0.12);
  transform: translateY(-2px);
}

.stat-icon {
  width: 48px;
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  border-radius: 12px;
  font-size: 1.5rem;
  color: white;
}

.stat-info {
  display: flex;
  flex-direction: column;
}

.stat-value {
  font-size: 1.5rem;
  font-weight: 700;
  color: white;
}

.stat-label {
  font-size: 0.875rem;
  color: rgba(255, 255, 255, 0.6);
}

/* 主内容区 */
.lobby-container {
  max-width: 1400px;
  margin: 0 auto;
  display: grid;
  grid-template-columns: 320px 1fr;
  gap: 2rem;
}

/* 快捷操作 */
.quick-actions {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.action-card {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 1.5rem;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  cursor: pointer;
  transition: all 0.3s ease;
}

.action-card:hover {
  background: rgba(255, 255, 255, 0.15);
  transform: translateX(8px);
}

.action-card.create-room .action-icon {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
}

.action-card.quick-match .action-icon {
  background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);
}

.action-icon {
  width: 56px;
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 14px;
  font-size: 1.5rem;
  color: white;
}

.action-text h3 {
  color: white;
  font-size: 1.1rem;
  font-weight: 600;
  margin-bottom: 0.25rem;
}

.action-text p {
  color: rgba(255, 255, 255, 0.6);
  font-size: 0.875rem;
}

/* 房间列表区 */
.room-section {
  background: rgba(255, 255, 255, 0.95);
  backdrop-filter: blur(20px);
  border-radius: 20px;
  padding: 1.5rem;
  min-height: 500px;
}

.section-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
  padding-bottom: 1rem;
  border-bottom: 1px solid #e5e7eb;
}

.section-header h2 {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 1.25rem;
  font-weight: 600;
  color: #1a1a2e;
  margin: 0;
}

/* v2.1.0 新增：实时更新标签 */
.live-tag {
  margin-left: 0.5rem;
  animation: pulse 2s infinite;
}

@keyframes pulse {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: 0.6;
  }
}

.room-list {
  display: grid;
  gap: 1rem;
  max-height: 600px;
  overflow-y: auto;
}

.room-card {
  background: white;
  border: 1px solid #e5e7eb;
  border-radius: 12px;
  padding: 1rem;
  cursor: pointer;
  transition: all 0.3s ease;
}

.room-card:hover:not(.disabled) {
  border-color: #667eea;
  box-shadow: 0 4px 12px rgba(102, 126, 234, 0.15);
  transform: translateY(-2px);
}

.room-card.disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.room-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.75rem;
}

.room-name {
  font-size: 1rem;
  font-weight: 600;
  color: #1a1a2e;
  margin: 0;
}

.room-body {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.75rem;
}

.room-host {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.host-avatar {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
}

.host-info {
  display: flex;
  flex-direction: column;
}

.host-name {
  font-size: 0.875rem;
  font-weight: 500;
  color: #374151;
}

.host-rating {
  display: flex;
  align-items: center;
  gap: 0.25rem;
  font-size: 0.75rem;
  color: #f59e0b;
}

.room-players {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.player-slots {
  display: flex;
  gap: 0.25rem;
}

.slot {
  width: 24px;
  height: 24px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 6px;
  background: #f3f4f6;
  color: #9ca3af;
  font-size: 0.75rem;
}

.slot.filled {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
}

.player-count {
  font-size: 0.875rem;
  color: #6b7280;
  font-weight: 500;
}

.room-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding-top: 0.75rem;
  border-top: 1px solid #f3f4f6;
}

.room-game-type {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 0.875rem;
  color: #6b7280;
}

/* 列表动画 */
.room-list-enter-active,
.room-list-leave-active {
  transition: all 0.3s ease;
}

.room-list-enter-from,
.room-list-leave-to {
  opacity: 0;
  transform: translateX(-30px);
}

/* 创建房间对话框 */
.create-room-dialog :deep(.el-dialog__body) {
  padding: 1.5rem;
}

.create-form :deep(.el-form-item__label) {
  font-weight: 500;
}

.game-mode-group {
  display: flex;
  width: 100%;
}

.game-mode-group :deep(.el-radio-button) {
  flex: 1;
}

.game-mode-group :deep(.el-radio-button__inner) {
  width: 100%;
}

/* 响应式 */
@media (max-width: 1024px) {
  .lobby-container {
    grid-template-columns: 1fr;
  }

  .quick-actions {
    flex-direction: row;
  }

  .action-card {
    flex: 1;
  }
}

@media (max-width: 768px) {
  .stats-bar {
    flex-wrap: wrap;
  }

  .stat-item {
    flex: 1 1 calc(50% - 0.75rem);
    min-width: 140px;
  }

  .quick-actions {
    flex-direction: column;
  }

  .action-card:hover {
    transform: none;
  }
}
</style>
