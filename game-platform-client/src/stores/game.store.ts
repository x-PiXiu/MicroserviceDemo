import { defineStore } from 'pinia'
import { ref, computed, readonly } from 'vue'
import { gomokuApi } from '@/api'
import { useAuthStore } from '@/stores/auth.store'
import type { Room, RoomDetail, Game, Move, CreateRoomRequest, JoinRoomRequest, LeaveRoomRequest, StartGameRequest, GameConfig, GameUserInfo } from '@/types'
import type { RoomListItem } from '@/types/websocket.types'
import type { GameSettlementData } from '@/types/gamedata.types'

/**
 * 成就通知类型
 */
export interface AchievementNotification {
  achievement_id: string
  achievement_name: string
  rarity?: string
  points?: number
  reward?: any
  timestamp?: number
}

/**
 * 排行榜变化类型
 */
export interface LeaderboardChange {
  type?: string
  scope?: string
  old_rank?: number
  new_rank?: number
  score?: number
}

/**
 * 游戏结果类型
 */
export interface GameResultData {
  result: string
  winnerPiece?: string
  gameStats?: any
}

/**
 * 聊天消息类型
 */
export interface ChatMessage {
  playerId: string
  message: string
  timestamp: number
}

export const useGameStore = defineStore('game', () => {
  // ==================== 基础状态 ====================
  const rooms = ref<Room[]>([])
  const currentRoom = ref<RoomDetail | null>(null)
  const currentGame = ref<Game | null>(null)
  const loading = ref(false)

  // ==================== v2.1.0 新增：WebSocket 连接状态 ====================
  /** WebSocket 是否已连接 */
  const wsConnected = ref(false)
  /** WebSocket 是否已订阅房间列表 */
  const wsSubscribed = ref(false)

  // ==================== v2.1.0 新增：当前房间简要信息（用于路由跳转） ====================
  /** 当前房间简要信息 */
  const currentRoomInfo = ref<{
    id: string
    isCreator: boolean
    pieceType?: 'black' | 'white'
    config?: any
    roomInfo?: any
  } | null>(null)

  // ==================== v2.1.0 新增：房间列表管理（实时推送） ====================

  /**
   * 转换 RoomListItem 到 Room 类型
   * v2.5.4: 兼容 camelCase 和 snake_case 两种格式
   */
  const convertRoomListItem = (item: any): Room => {
    // v2.5.4: 兼容 camelCase (roomId) 和 snake_case (room_id)
    const roomId = item.roomId || item.room_id
    const creatorId = item.creatorId || item.creator_id
    const maxPlayers = item.maxPlayers || item.max_players
    const createdAt = item.createdAt || item.created_at
    const state = item.roomState !== undefined ? item.roomState : item.state

    const players = item.players ? item.players.map((p: any) => ({
      id: p.playerId || p.player_id || p.id,
      username: p.username || p.nickname || (p.playerId || p.player_id || p.id)?.split('_')[0] || '玩家',
      rating: p.rating || 1000,
    })) : []

    return {
      id: roomId,
      name: `房间 ${roomId?.split('_').pop() || '未知'}`,
      status: state === 0 ? 'waiting' : state === 1 ? 'playing' : 'ended',
      host: {
        id: creatorId,
        username: creatorId?.split('_')[0] || '玩家',
        rating: 1000,
      },
      players,
      maxPlayers: maxPlayers || 2,
      gameType: 'gomoku',
      createdAt: createdAt ? new Date(createdAt * 1000).toISOString() : new Date().toISOString(),
    }
  }

  /**
   * 设置完整的房间列表
   */
  const setRooms = (newRooms: RoomListItem[]) => {
    rooms.value = newRooms.map(convertRoomListItem)
    console.log('[GameStore] Rooms updated:', rooms.value.length)
  }

  /**
   * 添加房间到列表
   * v2.5.4: 兼容 camelCase 和 snake_case
   */
  const addRoom = (room: any) => {
    const roomId = room.roomId || room.room_id
    const exists = rooms.value.find((r) => r.id === roomId)
    if (!exists) {
      rooms.value = [...rooms.value, convertRoomListItem(room)]
      console.log('[GameStore] Room added:', roomId)
    }
  }

  /**
   * 更新房间
   * v2.5.4: 兼容 camelCase 和 snake_case
   */
  const updateRoom = (roomId: string, updates: any) => {
    rooms.value = rooms.value.map((room) => {
      if (room.id === roomId) {
        const updatedRoom = { ...room }
        // v2.5.4: 兼容 state 和 roomState
        const state = updates.roomState !== undefined ? updates.roomState : updates.state
        if (state !== undefined) {
          updatedRoom.status = state === 0 ? 'waiting' : state === 1 ? 'playing' : 'ended'
        }
        // v2.5.4: 兼容 currentPlayers 和 current_players
        const currentPlayers = updates.currentPlayers ?? updates.current_players
        if (currentPlayers !== undefined) {
          // 更新 players 数组长度（仅用于显示）
          updatedRoom.players = Array(currentPlayers).fill(null).map((_, i) => ({
            id: `temp_${i}`,
            username: `玩家${i + 1}`,
            rating: 1000,
          }))
        }
        return updatedRoom
      }
      return room
    })
    console.log('[GameStore] Room updated:', roomId, updates)
  }

  /**
   * 从完整房间对象更新房间（服务端实际返回格式）
   * v2.5.4: 兼容 camelCase 和 snake_case
   */
  const updateRoomFromFullObject = (roomData: any) => {
    const roomId = roomData.roomId || roomData.room_id
    const exists = rooms.value.find((r) => r.id === roomId)
    if (exists) {
      // 更新现有房间
      rooms.value = rooms.value.map((room) => {
        if (room.id === roomId) {
          return convertRoomListItem(roomData)
        }
        return room
      })
      console.log('[GameStore] Room updated from full object:', roomId)
    } else {
      // 添加新房间
      rooms.value = [...rooms.value, convertRoomListItem(roomData)]
      console.log('[GameStore] Room added from full object:', roomId)
    }
  }

  /**
   * 移除房间
   */
  const removeRoom = (roomId: string) => {
    rooms.value = rooms.value.filter((room) => room.id !== roomId)
    console.log('[GameStore] Room removed:', roomId)
  }

  // ==================== v2.1.0 新增：连接状态管理 ====================

  /**
   * 设置 WebSocket 连接状态
   */
  const setWsConnected = (connected: boolean) => {
    wsConnected.value = connected
    if (!connected) {
      wsSubscribed.value = false
    }
    console.log('[GameStore] WebSocket connected:', connected)
  }

  /**
   * 设置房间列表订阅状态
   */
  const setWsSubscribed = (subscribed: boolean) => {
    wsSubscribed.value = subscribed
    console.log('[GameStore] Room list subscribed:', subscribed)
  }

  // ==================== v2.1.0 新增：当前房间信息管理 ====================

  /**
   * 设置当前房间信息（WebSocket 版）
   */
  const setCurrentRoom = (info: {
    id: string
    isCreator: boolean
    pieceType?: 'black' | 'white'
    config?: any
    roomInfo?: any
  }) => {
    currentRoomInfo.value = info
    console.log('[GameStore] Current room set:', info.id)
  }

  /**
   * 清除当前房间信息
   */
  const clearCurrentRoom = () => {
    currentRoomInfo.value = null
    currentRoom.value = null
    console.log('[GameStore] Current room cleared')
  }

  /**
   * 设置游戏开始状态
   */
  const setGameStarted = (data: {
    board: number[][]
    currentPlayer: number
    blackPlayer: string
    whitePlayer: string
    blackTimeLeft: number
    whiteTimeLeft: number
  }) => {
    // 初始化棋盘状态
    boardState.value = {
      board: data.board,
      currentPlayer: data.currentPlayer === 1 ? 'black' : 'white',
      gameResult: null,
      lastMove: null,
      totalMoves: 0,
      blackTimeLeft: data.blackTimeLeft,
      whiteTimeLeft: data.whiteTimeLeft,
    }

    // 更新玩家棋子映射
    playerPieces.value = {
      [data.blackPlayer]: 'black',
      [data.whitePlayer]: 'white',
    }

    console.log('[GameStore] Game started')
  }

  // ==================== 游戏实时状态 ====================
  /** 玩家准备状态映射 */
  const playerReadyStates = ref<Record<string, boolean>>({})

  /** 当前棋盘状态 */
  const boardState = ref<{
    board: number[][]
    currentPlayer: string
    gameResult: string | null
    lastMove: { row: number; col: number } | null
    totalMoves: number
    blackTimeLeft: number
    whiteTimeLeft: number
  } | null>(null)

  /** 游戏配置 */
  const gameConfig = ref<any>(null)

  /** 玩家棋子映射 */
  const playerPieces = ref<Record<string, string>>({})

  /** 观战者数量 */
  const spectatorCount = ref(0)

  // ==================== 游戏结果状态 ====================
  /** 游戏结果 */
  const gameResult = ref<GameResultData | null>(null)

  // ==================== v2.6.0 新增：游戏结算数据 ====================
  /**
   * 游戏结算数据
   * 文档依据: §7.3 GameEndEvent, §16.2.4 消息 8 game_settlement
   */
  const settlement = ref<GameSettlementData | null>(null)

  /** 结算弹窗是否可见 */
  const showSettlementModal = ref(false)

  /** 解锁的成就 */
  const unlockedAchievements = ref<any[]>([])

  /** 排行榜变化 */
  const leaderboardChanges = ref<LeaderboardChange | null>(null)

  /** 游戏奖励 */
  const gameRewards = ref<any>(null)

  // ==================== 成就通知队列 ====================
  /** 成就通知队列 */
  const achievementNotifications = ref<AchievementNotification[]>([])

  // ==================== 聊天消息 ====================
  /** 聊天消息列表 */
  const chatMessages = ref<ChatMessage[]>([])

  const availableRooms = computed(() =>
    rooms.value.filter((room) => room.status === 'waiting' && room.players.length < room.maxPlayers)
  )

  const getRooms = async (filter?: any) => {
    loading.value = true
    try {
      const response = await gomokuApi.getRooms(filter)
      rooms.value = response.rooms || []
      return rooms.value
    } finally {
      loading.value = false
    }
  }

  const createRoom = async (data: CreateRoomRequest) => {
    loading.value = true
    try {
      const result = await gomokuApi.createRoom(data)
      // 返回包含 roomId 的结果
      return { ...result, roomId: result.roomId, id: result.roomId }
    } finally {
      loading.value = false
    }
  }

  const joinRoom = async (roomId: string, playerId?: string) => {
    loading.value = true
    try {
      const requestData: JoinRoomRequest = {
        playerId: playerId || ''
      }
      const response = await gomokuApi.joinRoom(roomId, requestData)
      currentRoom.value = await gomokuApi.getRoomDetail(roomId)
      return response
    } finally {
      loading.value = false
    }
  }

  const leaveRoom = async (roomId: string, playerId?: string) => {
    loading.value = true
    try {
      const requestData: LeaveRoomRequest = {
        playerId: playerId || ''
      }
      await gomokuApi.leaveRoom(roomId, requestData)
      currentRoom.value = null
      currentGame.value = null
    } finally {
      loading.value = false
    }
  }

  const startGame = async (roomId: string, playerId?: string) => {
    loading.value = true
    try {
      const requestData: StartGameRequest = {
        playerId: playerId || ''
      }
      const response = await gomokuApi.startGame(roomId, requestData)
      return response
    } finally {
      loading.value = false
    }
  }

  const quickMatch = async (preferences?: any) => {
    loading.value = true
    try {
      const result = await gomokuApi.quickMatch(preferences || {})
      await joinRoom(result.roomId)
      return result
    } finally {
      loading.value = false
    }
  }

  const updateGame = (game: Game) => {
    currentGame.value = game
  }

  /**
   * 更新游戏玩家信息
   */
  const updateGamePlayers = (players: { black: GameUserInfo | null; white: GameUserInfo | null }, roomId?: string) => {
    console.log('[GameStore] updateGamePlayers called with:', players)
    console.log('[GameStore] currentGame.value exists:', !!currentGame.value)
    console.log('[GameStore] currentRoomInfo.value:', currentRoomInfo.value)
    console.log('[GameStore] currentRoom.value:', currentRoom.value)
    
    if (currentGame.value) {
      currentGame.value = {
        ...currentGame.value,
        players,
      }
      console.log('[GameStore] Game players updated (existing game):', players)
    } else {
      // 如果 currentGame 不存在，初始化它
      const now = new Date().toISOString()
      // 优先使用传入的 roomId，其次使用 currentRoomInfo，最后使用 currentRoom
      const gameId = roomId || currentRoomInfo.value?.id || currentRoom.value?.id || ''
      currentGame.value = {
        id: gameId,
        roomId: gameId,
        status: 'waiting',
        board: Array(15).fill(null).map(() => Array(15).fill(null)),
        currentPlayer: 'black',
        moves: [],
        players,
        createdAt: now,
        updatedAt: now,
      }
      console.log('[GameStore] Game players updated (new game initialized):', players)
      console.log('[GameStore] Initialized game with id:', gameId)
    }
  }

  const addMove = (move: Move) => {
    if (currentGame.value) {
      currentGame.value.moves.push(move)
      currentGame.value.currentPlayer = move.color === 'black' ? 'white' : 'black'
    }
  }

  const resetGame = () => {
    currentGame.value = null
    currentRoom.value = null
    // 重置游戏实时状态
    playerReadyStates.value = {}
    boardState.value = null
    gameConfig.value = null
    playerPieces.value = {}
    spectatorCount.value = 0
    // 重置游戏结果状态
    gameResult.value = null
    unlockedAchievements.value = []
    leaderboardChanges.value = null
    gameRewards.value = null
    // v2.6.0 新增：重置结算状态
    settlement.value = null
    showSettlementModal.value = false
    // 重置聊天
    chatMessages.value = []
  }

  // ==================== 玩家准备状态管理 ====================

  /**
   * 更新玩家准备状态
   */
  const updatePlayerReadyState = (playerId: string, ready: boolean) => {
    playerReadyStates.value = {
      ...playerReadyStates.value,
      [playerId]: ready,
    }
    console.log('[GameStore] Player ready state updated:', playerId, ready)
  }

  /**
   * 获取玩家准备状态
   */
  const isPlayerReady = (playerId: string): boolean => {
    return playerReadyStates.value[playerId] || false
  }

  /**
   * 检查所有玩家是否都准备好了
   */
  const allPlayersReady = computed(() => {
    if (!currentRoom.value || currentRoom.value.players.length === 0) return false
    return currentRoom.value.players.every(
      (player) => playerReadyStates.value[player.id] === true
    )
  })

  // ==================== 游戏状态管理 ====================

  /**
   * 更新游戏状态（从 game_state 消息）
   */
  const updateGameState = (data: {
    board: number[][]
    currentPlayer: string | number
    gameResult: string | number | null
    lastMove: { row: number; col: number } | null
    totalMoves: number
    blackTimeLeft?: number
    whiteTimeLeft?: number
    config?: any
    playerPieces?: Record<string, string | number>
    spectatorCount?: number
  }) => {
    // 转换 currentPlayer 类型
    const currentPlayerStr = typeof data.currentPlayer === 'number'
      ? (data.currentPlayer === 1 ? 'black' : 'white')
      : data.currentPlayer

    // 转换 gameResult 类型
    const gameResultStr = data.gameResult === null ? null : String(data.gameResult)

    // 转换 playerPieces 类型
    const playerPiecesStr: Record<string, string> = {}
    if (data.playerPieces) {
      Object.entries(data.playerPieces).forEach(([playerId, piece]) => {
        playerPiecesStr[playerId] = typeof piece === 'number'
          ? (piece === 1 ? 'black' : 'white')
          : piece
      })
    }

    boardState.value = {
      board: data.board,
      currentPlayer: currentPlayerStr,
      gameResult: gameResultStr,
      lastMove: data.lastMove,
      totalMoves: data.totalMoves,
      blackTimeLeft: data.blackTimeLeft ?? 0,
      whiteTimeLeft: data.whiteTimeLeft ?? 0,
    }

    if (data.config) {
      gameConfig.value = data.config
    }

    if (data.playerPieces) {
      playerPieces.value = playerPiecesStr
    }

    if (data.spectatorCount !== undefined) {
      spectatorCount.value = data.spectatorCount
    }

    console.log('[GameStore] Game state updated')
  }

  /**
   * 更新当前玩家（轮次变化）
   */
  const updateCurrentPlayer = (player: string) => {
    if (boardState.value) {
      boardState.value = {
        ...boardState.value,
        currentPlayer: player,
      }
    }
    console.log('[GameStore] Current player updated:', player)
  }

  /**
   * 落子后更新棋盘
   */
  const updateBoardAfterMove = (position: { row: number; col: number }) => {
    if (boardState.value) {
      const newBoard = boardState.value.board.map((row) => [...row])
      const piece = boardState.value.currentPlayer === 'black' ? 1 : 2
      newBoard[position.row][position.col] = piece

      boardState.value = {
        ...boardState.value,
        board: newBoard,
        lastMove: position,
        totalMoves: boardState.value.totalMoves + 1,
        currentPlayer: boardState.value.currentPlayer === 'black' ? 'white' : 'black',
      }
    }
    console.log('[GameStore] Board updated after move:', position)
  }

  /**
   * 更新时间
   */
  const updateTime = (data: { blackTime: number; whiteTime: number }) => {
    if (boardState.value) {
      boardState.value = {
        ...boardState.value,
        blackTimeLeft: data.blackTime,
        whiteTimeLeft: data.whiteTime,
      }
    }
  }

  /**
   * 更新游戏配置
   */
  const updateGameConfig = (config: Partial<GameConfig>) => {
    gameConfig.value = { ...gameConfig.value, ...config } as GameConfig
    console.log('[GameStore] Game config updated:', config)
  }

  /**
   * 更新玩家棋子
   */
  const updatePlayerPiece = (playerId: string, piece: number) => {
    const pieceStr = piece === 1 ? 'black' : 'white'
    playerPieces.value = {
      ...playerPieces.value,
      [playerId]: pieceStr,
    }
    console.log('[GameStore] Player piece updated:', playerId, pieceStr)
  }

  /**
   * 设置我的棋子
   */
  const myPiece = ref<'black' | 'white' | null>(null)
  const setMyPiece = (piece: 'black' | 'white') => {
    myPiece.value = piece
    console.log('[GameStore] My piece set:', piece)
  }

  /**
   * 更新棋盘（悔棋后）
   */
  const updateBoard = (board: number[][], totalMoves: number) => {
    if (boardState.value) {
      boardState.value = {
        ...boardState.value,
        board,
        totalMoves,
      }
    }
    console.log('[GameStore] Board updated, total moves:', totalMoves)
  }

  // ==================== 游戏结果管理 ====================

  /**
   * 设置游戏结果
   */
  const setGameResult = (data: GameResultData) => {
    gameResult.value = data
    console.log('[GameStore] Game result set:', data.result)
  }

  /**
   * 设置解锁的成就
   */
  const setUnlockedAchievements = (achievements: any[]) => {
    unlockedAchievements.value = achievements
    console.log('[GameStore] Unlocked achievements:', achievements.length)
  }

  /**
   * 设置排行榜变化
   */
  const setLeaderboardChanges = (changes: LeaderboardChange) => {
    leaderboardChanges.value = changes
    console.log('[GameStore] Leaderboard changes:', changes)
  }

  /**
   * 设置游戏奖励
   */
  const setGameRewards = (rewards: any) => {
    gameRewards.value = rewards
    console.log('[GameStore] Game rewards:', rewards)
  }

  // ==================== v2.6.0 新增：结算管理 ====================

  /**
   * 设置结算数据
   * 文档依据: §16.2.4 消息 8 game_settlement
   */
  const setSettlement = (data: GameSettlementData) => {
    settlement.value = data
    showSettlementModal.value = true
    console.log('[GameStore] Settlement data set:', data)
  }

  /**
   * 清除结算数据
   */
  const clearSettlement = () => {
    settlement.value = null
    showSettlementModal.value = false
    console.log('[GameStore] Settlement cleared')
  }

  /**
   * 关闭结算弹窗
   */
  const closeSettlementModal = () => {
    showSettlementModal.value = false
  }

  /**
   * 检查是否有段位变化
   */
  const hasTierChange = computed(() => {
    if (!settlement.value) return false
    const tierBefore = settlement.value.tier?.before
    const tierAfter = settlement.value.tier?.after
    if (tierBefore === undefined || tierAfter === undefined) return false
    return tierBefore !== tierAfter
  })

  /**
   * 是否为升级
   */
  const isTierUpgrade = computed(() => {
    if (!settlement.value) return false
    const tierBefore = settlement.value.tier?.before
    const tierAfter = settlement.value.tier?.after
    if (tierBefore === undefined || tierAfter === undefined) return false
    return tierAfter > tierBefore
  })

  /**
   * 是否有奖励
   */
  const hasRewards = computed(() => {
    if (!settlement.value?.rewards) return false
    const { gold, experience, honor } = settlement.value.rewards
    return gold > 0 || experience > 0 || honor > 0
  })

  /**
   * 是否有成就解锁
   */
  const hasAchievements = computed(() => {
    return (settlement.value?.achievements_unlocked?.length || 0) > 0
  })

  // ==================== 成就通知管理 ====================

  /**
   * 添加成就通知
   */
  const addAchievementNotification = (notification: AchievementNotification) => {
    achievementNotifications.value.push({
      ...notification,
      timestamp: notification.timestamp || Date.now(),
    })
    console.log('[GameStore] Achievement notification added:', notification.achievement_name)
  }

  /**
   * 移除成就通知
   */
  const removeAchievementNotification = (index: number) => {
    achievementNotifications.value.splice(index, 1)
  }

  /**
   * 清空成就通知
   */
  const clearAchievementNotifications = () => {
    achievementNotifications.value = []
  }

  // ==================== 聊天管理 ====================

  /**
   * 添加聊天消息
   */
  const addChatMessage = (message: ChatMessage) => {
    chatMessages.value.push({
      ...message,
      timestamp: message.timestamp || Date.now(),
    })
  }

  /**
   * 清空聊天消息
   */
  const clearChatMessages = () => {
    chatMessages.value = []
  }

  // ==================== 观战者管理 ====================

  /**
   * 更新观战者数量
   */
  const updateSpectatorCount = (count: number) => {
    spectatorCount.value = count
  }

  // ==================== 房间玩家管理 ====================

  /**
   * 添加玩家到房间
   */
  const addPlayerToRoom = (player: any, color?: string) => {
    console.log('[GameStore] addPlayerToRoom called:', { player, color })

    if (currentRoom.value) {
      currentRoom.value = {
        ...currentRoom.value,
        players: [...currentRoom.value.players, player],
      }

      if (color) {
        playerPieces.value = {
          ...playerPieces.value,
          [player.id]: color,
        }
      }
    }

    // 同时更新 currentGame 的玩家信息
    if (currentGame.value && currentGame.value.players) {
      const updatedPlayers = { ...currentGame.value.players }

      // 根据 color 分配到黑方或白方
      if (color === 'black' || color === '1') {
        if (!updatedPlayers.black || updatedPlayers.black.id === player.id) {
          updatedPlayers.black = {
            id: player.id,
            username: player.username || player.nickname || player.id.split('_')[0] || '玩家',
            rating: player.rating || 1000,
          }
          console.log('[GameStore] Added black player to currentGame:', player.id)
        }
      } else if (color === 'white' || color === '2') {
        if (!updatedPlayers.white || updatedPlayers.white.id === player.id) {
          updatedPlayers.white = {
            id: player.id,
            username: player.username || player.nickname || player.id.split('_')[0] || '玩家',
            rating: player.rating || 1000,
          }
          console.log('[GameStore] Added white player to currentGame:', player.id)
        }
      } else {
        // 如果没有指定颜色，根据当前玩家数量分配
        if (!updatedPlayers.black) {
          updatedPlayers.black = {
            id: player.id,
            username: player.username || player.nickname || player.id.split('_')[0] || '玩家',
            rating: player.rating || 1000,
          }
          console.log('[GameStore] Added black player to currentGame (auto):', player.id)
        } else if (!updatedPlayers.white) {
          updatedPlayers.white = {
            id: player.id,
            username: player.username || player.nickname || player.id.split('_')[0] || '玩家',
            rating: player.rating || 1000,
          }
          console.log('[GameStore] Added white player to currentGame (auto):', player.id)
        }
      }

      currentGame.value = {
        ...currentGame.value,
        players: updatedPlayers,
      }
      console.log('[GameStore] currentGame players updated:', updatedPlayers)
    }

    console.log('[GameStore] Player added to room:', player.id)
  }

  /**
   * 从房间移除玩家
   */
  const removePlayerFromRoom = (playerId: string) => {
    if (currentRoom.value) {
      currentRoom.value = {
        ...currentRoom.value,
        players: currentRoom.value.players.filter((p) => p.id !== playerId),
      }
    }

    // 同时更新 currentGame 的玩家信息
    if (currentGame.value) {
      const updatedPlayers = { ...currentGame.value.players }
      if (updatedPlayers.black?.id === playerId) {
        updatedPlayers.black = null
      }
      if (updatedPlayers.white?.id === playerId) {
        updatedPlayers.white = null
      }
      currentGame.value = {
        ...currentGame.value,
        players: updatedPlayers,
      }
    }

    // 移除准备状态
    const newReadyStates = { ...playerReadyStates.value }
    delete newReadyStates[playerId]
    playerReadyStates.value = newReadyStates

    // 移除棋子映射
    const newPlayerPieces = { ...playerPieces.value }
    delete newPlayerPieces[playerId]
    playerPieces.value = newPlayerPieces

    console.log('[GameStore] Player removed from room:', playerId)
  }

  // ==================== 当前玩家信息 ====================

  /**
   * 获取当前用户的棋子颜色
   * 注意：需要在组件中调用，确保 authStore 已初始化
   */
  const getMyPiece = () => {
    const authStore = useAuthStore()
    const myId = authStore.user?.id
    return myId ? playerPieces.value[myId] : null
  }

  /**
   * 检查是否轮到我
   */
  const checkIsMyTurn = () => {
    const myPiece = getMyPiece()
    if (!boardState.value || !myPiece) return false
    return boardState.value.currentPlayer === myPiece
  }

  return {
    // 基础状态
    rooms,
    currentRoom,
    currentGame,
    loading: readonly(loading),
    availableRooms,

    // v2.1.0 新增：WebSocket 连接状态
    wsConnected: readonly(wsConnected),
    wsSubscribed: readonly(wsSubscribed),
    setWsConnected,
    setWsSubscribed,

    // v2.1.0 新增：当前房间简要信息
    currentRoomInfo: readonly(currentRoomInfo),
    setCurrentRoom,
    clearCurrentRoom,

    // v2.1.0 新增：房间列表管理
    setRooms,
    addRoom,
    updateRoom,
    updateRoomFromFullObject,
    removeRoom,

    // v2.1.0 新增：游戏开始
    setGameStarted,

    // 游戏实时状态
    playerReadyStates: readonly(playerReadyStates),
    boardState: readonly(boardState),
    gameConfig: readonly(gameConfig),
    playerPieces: readonly(playerPieces),
    spectatorCount: readonly(spectatorCount),

    // 游戏结果状态
    gameResult: readonly(gameResult),
    unlockedAchievements: readonly(unlockedAchievements),
    leaderboardChanges: readonly(leaderboardChanges),
    gameRewards: readonly(gameRewards),

    // v2.6.0 新增：结算状态
    settlement: readonly(settlement),
    showSettlementModal: readonly(showSettlementModal),
    hasTierChange,
    isTierUpgrade,
    hasRewards,
    hasAchievements,

    // 成就通知
    achievementNotifications: readonly(achievementNotifications),

    // 聊天
    chatMessages: readonly(chatMessages),

    // 计算属性
    allPlayersReady,

    // 当前玩家信息
    getMyPiece,
    checkIsMyTurn,

    // 房间操作
    getRooms,
    createRoom,
    joinRoom,
    leaveRoom,
    startGame,
    quickMatch,

    // 游戏操作
    updateGame,
    updateGamePlayers,
    addMove,
    resetGame,

    // 玩家准备状态
    updatePlayerReadyState,
    isPlayerReady,

    // 游戏状态管理
    updateGameState,
    updateCurrentPlayer,
    updateBoardAfterMove,
    updateTime,
    updateGameConfig,
    updatePlayerPiece,
    myPiece: readonly(myPiece),
    setMyPiece,
    updateBoard,

    // 游戏结果管理
    setGameResult,
    setUnlockedAchievements,
    setLeaderboardChanges,
    setGameRewards,

    // v2.6.0 新增：结算管理
    setSettlement,
    clearSettlement,
    closeSettlementModal,

    // 成就通知
    addAchievementNotification,
    removeAchievementNotification,
    clearAchievementNotifications,

    // 聊天管理
    addChatMessage,
    clearChatMessages,

    // 观战者管理
    updateSpectatorCount,

    // 房间玩家管理
    addPlayerToRoom,
    removePlayerFromRoom,
  }
})
