import type { GameUserInfo } from './game.types'

// ==================== 客户端发送的消息 ====================

/** 认证消息 */
export interface AuthenticateMessage {
  type: 'authenticate'
  data: {
    token: string
    playerId: string
  }
}

/** 准备状态消息 */
export interface ReadyMessage {
  type: 'ready'
  data: {
    ready: boolean
  }
}

/** 落子消息 */
export interface PlacePieceMessage {
  type: 'place_piece'
  data: {
    position: {
      row: number
      col: number
    }
  }
}

/** 悔棋消息 */
export interface UndoMoveMessage {
  type: 'undo_move'
  data: Record<string, never>
}

/** 认输消息 */
export interface SurrenderMessage {
  type: 'surrender'
  data: Record<string, never>
}

/** 和棋提议 */
export interface DrawOfferMessage {
  type: 'draw_offer'
  data: Record<string, never>
}

/** 和棋响应 */
export interface DrawResponseMessage {
  type: 'draw_response'
  data: {
    accepted: boolean  // 修复：与文档一致
  }
}

/** 悔棋响应 */
export interface UndoResponseMessage {
  type: 'undo_response'
  data: {
    accepted: boolean  // 修复：与文档一致
  }
}

/** 聊天消息 */
export interface ChatMessage {
  type: 'chat'
  data: {
    message: string
    timestamp?: number
  }
}

/** 心跳消息 */
export interface HeartbeatMessage {
  type: 'heartbeat'
  data: Record<string, never>
}

/** 加入房间消息 */
export interface JoinRoomMessage {
  type: 'join_room'
  data: {
    roomId: string
    role?: 'player' | 'spectator'
  }
}

/** 离开房间消息 */
export interface LeaveRoomMessage {
  type: 'leave_room'
  data: Record<string, never>
}

// ===== v2.1.0 新增：房间列表订阅与房间操作 =====

/** 订阅房间列表消息 */
export interface SubscribeRoomListMessage {
  type: 'subscribe_room_list'
  data: {
    gameType?: string  // 游戏类型，如 'gomoku'
  }
}

/** 取消订阅房间列表消息 */
export interface UnsubscribeRoomListMessage {
  type: 'unsubscribe_room_list'
  data: Record<string, never>
}

/** 创建房间消息（WebSocket 版） */
export interface CreateRoomMessage {
  type: 'create_room'
  data: {
    creatorId: string
    config: {
      gameMode: string      // freestyle, renju, swap2
      timeLimit: number     // 基础思考时间(秒)
      incrementPerMove: number  // 每步增加时间(秒)
      allowUndo: boolean
      allowSpectators: boolean
      maxSpectators: number
    }
  }
}

/** 进入房间页面消息（已加入的房间） */
export interface EnterRoomMessage {
  type: 'enter_room'
  data: {
    roomId: string
  }
}

/** 开始游戏消息（WebSocket 版，房主调用） */
export interface StartGameMessage {
  type: 'start_game'
  data: Record<string, never>
}

/** 房间操作消息 v2.3.0 */
export interface RoomOperationMessage {
  type: 'room_operation'
  data: {
    operation: 'start_game' | 'kick_player' | 'transfer_host' | 'change_config'
    roomId?: string
    targetPlayerId?: string
    config?: Record<string, unknown>
  }
}

// ===== v2.6.0 新增：匹配系统消息 =====

/** 开始匹配消息 */
export interface MatchStartMessage {
  type: 'match_start'
  data: {
    game_type: string
    match_mode?: 'ranked' | 'casual'
    rating_range?: {
      min: number
      max: number
    }
  }
}

/** 取消匹配消息 */
export interface MatchCancelMessage {
  type: 'match_cancel'
  data: Record<string, never>
}

/** 客户端消息联合类型 */
export type ClientMessage =
  | AuthenticateMessage
  | ReadyMessage
  | PlacePieceMessage
  | UndoMoveMessage
  | SurrenderMessage
  | DrawOfferMessage
  | DrawResponseMessage
  | UndoResponseMessage
  | ChatMessage
  | HeartbeatMessage
  | JoinRoomMessage
  | LeaveRoomMessage
  // v2.1.0 新增
  | SubscribeRoomListMessage
  | UnsubscribeRoomListMessage
  | CreateRoomMessage
  | EnterRoomMessage
  | StartGameMessage
  // v2.3.0 新增
  | RoomOperationMessage
  // v2.6.0 新增：匹配系统
  | MatchStartMessage
  | MatchCancelMessage

// ==================== 服务端推送的消息 ====================

/** 欢迎消息 */
export interface WelcomeMessage {
  type: 'welcome'
  player_id: string
  server_info: {
    name: string
    version: string
    features: string[]
  }
  timestamp: number
}

/** 认证成功 */
export interface AuthSuccessMessage {
  type: 'auth_success'
  data: {
    playerId: string
    authenticated: boolean
  }
  timestamp: number
}

/** 认证失败 */
export interface AuthFailedMessage {
  type: 'auth_failed'
  data: {
    error: string
    message: string
  }
  timestamp: number
}

/** 房间配置 */
export interface RoomConfigMessage {
  type: 'room_config'
  data: {
    gameMode: string
    gameModeStr?: string
    timeLimit: number
    incrementPerMove: number
    allowUndo: boolean
    allowSpectators: boolean
    maxSpectators: number
    rankingEnabled?: boolean
  }
  roomId: string
  timestamp: number
}

/** 棋子分配 */
export interface PlayerPieceAssignedMessage {
  type: 'player_piece_assigned'
  data: {
    playerId: string
    piece: number  // 1=黑, 2=白
  }
  roomId: string
  timestamp: number
}

/** 准备状态确认 */
export interface ReadyConfirmMessage {
  type: 'ready_confirm'
  data: {
    playerId: string
    ready: boolean
  }
  timestamp: number
}

/** 游戏状态更新 */
export interface GameStateMessage {
  type: 'game_state'
  data: {
    board: {
      board: number[][]
      currentPlayer: number
      gameResult: number
      lastMove: { row: number; col: number } | { row: -1; col: -1 }
      totalMoves: number
      blackTimeLeft: number
      whiteTimeLeft: number
      canUndo: boolean
      gameMode: number
      incrementPerMove: number
      forbiddenPositions: Array<{ row: number; col: number }>
      moveHistory: Array<{ row: number; col: number; player: number }>
    }
    config: {
      gameMode: number
      gameModeStr: string
      timeLimit: number
      incrementPerMove: number
      allowUndo: boolean
      allowSpectators: boolean
      maxSpectators: number
      rankingEnabled: boolean
    }
    currentTurn: string
    playerPieces: Record<string, number>
    spectatorCount: number
  }
  roomId: string
  timestamp: number
}

/** 移动结果 */
export interface MoveResultMessage {
  type: 'move_result'
  data: {
    playerId: string
    position: { row: number; col: number }
    success: boolean
  }
  roomId: string
  timestamp: number
}

/** 游戏结束 */
export interface GameEndMessage {
  type: 'game_end'
  data: {
    result: number
    winnerId?: string       // 添加：获胜者ID
    winnerPiece: number | null
    reason?: string         // 添加：游戏结束原因
    gameStats: {
      totalMoves: number
      gameDuration: number
    }
    achievements?: Array<{  // 添加：成就列表
      achievement_id: string
      achievement_name: string
      rarity: 'COMMON' | 'RARE' | 'EPIC' | 'LEGENDARY'
      points: number
      newly_unlocked: boolean
      reward: {
        gold: number
        gems: number
        honor_points: number
        experience: number
      }
    }>
    leaderboard_changes?: { // 添加：排行榜变化
      rating: {
        before: number
        after: number
        change: number
        rank_before: number
        rank_after: number
      }
      win_streak: {
        current: number
        best: number
      }
    }
    rewards?: {             // 添加：奖励
      experience: number
      gold: number
      rating_change: number
    }
  }
  roomId: string
  timestamp: number
}

/** 时间更新 */
export interface TimeUpdateMessage {
  type: 'time_update'
  data: {
    blackTime: number
    whiteTime: number
  }
  roomId: string
  timestamp: number
}

/** 悔棋请求通知 */
export interface UndoRequestMessage {
  type: 'undo_request'
  data: {
    fromPlayer: string
  }
  roomId: string
  timestamp: number
}

/** 悔棋成功 */
export interface MoveUndoneMessage {
  type: 'move_undone'
  data: {
    playerId: string
    board: number[][]
    totalMoves: number
  }
  roomId: string
  timestamp: number
}

/** 玩家认输 */
export interface PlayerSurrenderedMessage {
  type: 'player_surrendered'
  data: {
    playerId: string
    piece: number
  }
  roomId: string
  timestamp: number
}

/** 和棋提议通知 */
export interface DrawOfferNotification {
  type: 'draw_offer'
  data: {
    fromPlayer: string
  }
  roomId: string
  timestamp: number
}

/** 和棋响应通知 */
export interface DrawResponseNotification {
  type: 'draw_response'
  data: {
    fromPlayer: string
    accepted: boolean
  }
  roomId: string
  timestamp: number
}

/** 系统通知 */
export interface NotificationMessage {
  type: 'notification'
  data: {
    type: string
    message: string
    timestamp: number
  }
  roomId?: string
  timestamp: number
}

/** 聊天消息广播 */
export interface ChatBroadcastMessage {
  type: 'chat'
  data: {
    playerId: string
    message: string
    timestamp: number
  }
  roomId: string
  timestamp: number
}

/** 观战者变化通知 */
export interface SpectatorChangeMessage {
  type: 'spectator_change'
  data: {
    spectatorId: string
    joined: boolean
    spectatorCount: number
  }
  roomId: string
  timestamp: number
}

/** 错误消息 */
export interface ErrorMessage {
  type: 'error'
  error_code: string
  error_message: string
  player_id?: string
  timestamp: number
}

/** 成就解锁实时通知 (v2.0.3) */
export interface AchievementUnlockedMessage {
  type: 'achievement_unlocked'
  data: {
    achievement_id: string
    achievement_name: string
    rarity: 'COMMON' | 'RARE' | 'EPIC' | 'LEGENDARY'
    points: number
    newly_unlocked: boolean
    reward: {
      gold: number
      gems: number
      honor_points: number
      experience: number
    }
  }
  timestamp: number
}

/** 排行榜更新实时通知 (v2.0.3) */
export interface LeaderboardUpdateMessage {
  type: 'leaderboard_update'
  data: {
    type: string
    scope: string
    old_rank: number
    new_rank: number
    score: number
  }
  timestamp: number
}

/** 货币更新通知 (v2.0.3) */
export interface CurrencyUpdateMessage {
  type: 'currency_update'
  data: {
    currencyType: 'coins' | 'gems' | 'honor_points' | 'tokens'
    newAmount: number
    change: number
  }
  timestamp: number
}

/** 玩家准备状态通知 */
export interface PlayerReadyMessage {
  type: 'player_ready'
  data: {
    playerId: string
    ready: boolean
  }
  timestamp: number
}

/** 全体玩家准备状态通知 */
export interface AllPlayersReadyMessage {
  type: 'all_players_ready'
  data: {
    players: Record<string, boolean>
    canStart: boolean
  }
  timestamp: number
}

// ===== v2.1.0 新增：房间列表与房间操作响应 =====

/** 房间数据结构（用于房间列表） */
export interface RoomListItem {
  room_id: string
  creator_id: string
  state: number  // 0=等待中, 1=游戏中, 2=已结束
  current_players: number
  max_players: number
  game_type: number
  created_at: number
  players?: Array<{
    player_id?: string
    id?: string
    username?: string
    nickname?: string
    rating?: number
  }>
  gomokuStats?: {
    gameMode: number
    spectatorCount: number
  }
}

/** 房间列表消息 */
export interface RoomListMessage {
  type: 'room_list'
  data: {
    rooms: RoomListItem[]
  }
  timestamp: number
}

/** 订阅确认消息 */
export interface SubscriptionConfirmMessage {
  type: 'subscription_confirm'
  data: {
    subscribed: boolean
    topic: string
    rooms?: RoomListItem[]
  }
  timestamp: number
}

/** 新房间创建通知 */
export interface RoomAddedMessage {
  type: 'room_added'
  data: {
    room: RoomListItem
  }
  timestamp: number
}

/** 房间更新通知 */
export interface RoomUpdatedMessage {
  type: 'room_updated'
  data: {
    room_id: string
    changes: Partial<RoomListItem>
  }
  timestamp: number
}

/** 房间移除通知 */
export interface RoomRemovedMessage {
  type: 'room_removed'
  data: {
    room_id: string
  }
  timestamp: number
}

/** 房间创建成功（服务端实际返回格式） */
export interface CreateRoomSuccessMessage {
  type: 'create_room_success'
  data: {
    playerId: string
    roomId?: string  // 可选，服务端可能通过 room_added 发送
  }
  timestamp: number
}

/** 房间创建结果（文档定义格式） */
export interface RoomCreatedMessage {
  type: 'room_created'
  data: {
    roomId: string
    creatorId: string
    pieceType: 'black' | 'white'
    config: {
      gameMode: number
      gameModeStr: string
      timeLimit: number
      incrementPerMove: number
      allowUndo: boolean
      allowSpectators: boolean
      maxSpectators: number
    }
    message: string
  }
  timestamp: number
}

/** 房间加入结果 */
export interface RoomJoinedMessage {
  type: 'room_joined'
  data: {
    roomId: string
    playerId: string
    role: 'player' | 'spectator'
    pieceType?: 'black' | 'white' | 'none'
    roomInfo: {
      creatorId?: string
      creator_id?: string  // 兼容 snake_case
      currentPlayers?: number
      maxPlayers?: number
      spectatorCount?: number
      roomState?: string
      players?: Array<{
        player_id: string
        username?: string
        nickname?: string
        rating?: number
        level?: number
        piece?: number | string  // 1=黑, 2=白
      }>
    }
  }
  timestamp: number
}

/** 房间离开结果 */
export interface RoomLeftMessage {
  type: 'room_left'
  data: {
    roomId: string
    playerId: string
  }
  timestamp: number
}

/** 玩家加入通知（广播给房间内其他玩家） */
export interface PlayerJoinedMessage {
  type: 'player_joined'
  data: {
    playerId: string
    pieceType: 'black' | 'white'
    playerCount: number
    playerInfo?: {
      username: string
      rating: number
    }
  }
  roomId: string
  timestamp: number
}

/** 玩家离开通知（广播给房间内其他玩家） */
export interface PlayerLeftMessage {
  type: 'player_left'
  data: {
    playerId: string
    pieceType?: 'black' | 'white'
    playerCount: number
    reason: string
  }
  roomId: string
  timestamp: number
}

/** 游戏开始广播 */
export interface GameStartedMessage {
  type: 'game_started'
  data: {
    board: number[][]
    currentPlayer: number  // 1=黑, 2=白
    blackPlayer: string
    whitePlayer: string
    blackTimeLeft: number
    whiteTimeLeft: number
  }
  roomId: string
  timestamp: number
}

/**
 * 游戏结算消息
 *
 * 文档依据: §16.2.4 消息 8 game_settlement
 * 服务端在游戏结束后单独推送给每个玩家
 *
 * 注意：服务端使用嵌套结构 (rating.before, tier.after 等)
 */
export interface GameSettlementMessage {
  type: 'game_settlement'
  data: {
    user_id: string
    result: 'win' | 'lose' | 'draw'

    // 评分变化（嵌套结构）
    rating: {
      before: number
      after: number
      change: number
    }

    // 段位变化（嵌套结构）
    tier: {
      before: number
      after: number
      name_before: string
      name_after: string
    }

    // 奖励
    rewards: {
      gold: number
      experience: number
      honor: number
      honor_points?: number  // 向后兼容
      gem?: number
      gems?: number          // 向后兼容
    }

    // 统计（可选）
    stats?: {
      total_games?: number
      wins?: number
      losses?: number
      draws?: number
      current_win_streak?: number
      best_win_streak?: number
    }

    // 成就
    achievements_unlocked?: Array<{
      id: string
      name: string
      rarity: 'COMMON' | 'RARE' | 'EPIC' | 'LEGENDARY'
    }>

    // 连胜
    new_win_streak?: number

    // 排行榜变化（可选）
    leaderboard_rank_change?: {
      old_rank: number
      new_rank: number
    }
  }
  timestamp: number
}

// ===== v2.6.0 新增：匹配系统服务端消息 =====

/**
 * 匹配状态更新消息
 *
 * 文档依据: game_platform_gameplay_design.md §16.2.4
 * 服务端推送匹配队列状态
 */
export interface MatchStatusUpdateMessage {
  type: 'match_status_update'
  data: {
    status: 'searching' | 'matched' | 'timeout' | 'cancelled'
    queue_size?: number
    estimated_wait_time?: number
    match_id?: string
  }
  timestamp: number
}

/**
 * 匹配成功消息
 *
 * 文档依据: game_platform_gameplay_design.md §16.2.4
 * 服务端推送匹配成功，包含对手信息和房间信息
 */
export interface MatchFoundMessage {
  type: 'match_found'
  data: {
    match_id: string
    room_id: string
    opponent: {
      user_id: string
      username: string
      rating: number
      tier: number
      tier_name: string
    }
    your_piece: 1 | 2  // 1=黑, 2=白
    config: {
      game_mode: string
      time_limit: number
      increment_per_move: number
    }
  }
  timestamp: number
}

/**
 * 匹配超时消息
 *
 * 文档依据: game_platform_gameplay_design.md §16.2.4
 * 服务端推送匹配超时
 */
export interface MatchTimeoutMessage {
  type: 'match_timeout'
  data: {
    reason: string
    wait_time: number
    can_retry: boolean
  }
  timestamp: number
}

/** 服务端消息联合类型 */
export type ServerMessage =
  | WelcomeMessage
  | AuthSuccessMessage
  | AuthFailedMessage
  | RoomConfigMessage
  | PlayerPieceAssignedMessage
  | ReadyConfirmMessage
  | PlayerReadyMessage
  | AllPlayersReadyMessage
  | GameStateMessage
  | MoveResultMessage
  | GameEndMessage
  | TimeUpdateMessage
  | UndoRequestMessage
  | MoveUndoneMessage
  | PlayerSurrenderedMessage
  | DrawOfferNotification
  | DrawResponseNotification
  | NotificationMessage
  | ChatBroadcastMessage
  | SpectatorChangeMessage
  | AchievementUnlockedMessage
  | LeaderboardUpdateMessage
  | CurrencyUpdateMessage
  | ErrorMessage
  // v2.1.0 新增
  | RoomListMessage
  | SubscriptionConfirmMessage
  | RoomAddedMessage
  | RoomUpdatedMessage
  | RoomRemovedMessage
  | RoomCreatedMessage
  | CreateRoomSuccessMessage
  | RoomJoinedMessage
  | RoomLeftMessage
  | PlayerJoinedMessage
  | PlayerLeftMessage
  | GameStartedMessage
  // v2.6.0 新增：游戏结算
  | GameSettlementMessage
  // v2.6.0 新增：匹配系统
  | MatchStatusUpdateMessage
  | MatchFoundMessage
  | MatchTimeoutMessage

// ==================== 房间信息 ====================

export interface RoomInfo {
  id: string
  name: string
  players: { black: GameUserInfo | null; white: GameUserInfo | null }
  status: string
}

// ==================== 游戏模式枚举 ====================

export const GameMode = {
  FREESTYLE: 0,
  RENJU: 1,
  SWAP2: 2,
  PRO: 3,
  TOURNAMENT: 4,
} as const

export type GameModeType = (typeof GameMode)[keyof typeof GameMode]

export const GameModeLabels: Record<GameModeType, string> = {
  [GameMode.FREESTYLE]: '自由模式',
  [GameMode.RENJU]: '连珠模式',
  [GameMode.SWAP2]: 'Swap2规则',
  [GameMode.PRO]: '职业规则',
  [GameMode.TOURNAMENT]: '锦标赛规则',
}

// ==================== 游戏结果枚举 ====================
// 注意: gamedata.types.ts 中定义了 GameResult 作为字符串字面量类型
// 这里的常量对象重命名为避免冲突

export const GameResultConst = {
  NONE: 0,
  BLACK_WIN: 1,
  WHITE_WIN: 2,
  DRAW: 3,
} as const

export type GameResultType = (typeof GameResultConst)[keyof typeof GameResultConst]
