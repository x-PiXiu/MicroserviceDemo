// ==================== 位置与颜色 ====================

export interface Position {
  x: number
  y: number
}

export type PlayerColor = 'black' | 'white'
export type GameStatus = 'waiting' | 'playing' | 'ended'
export type GameOverReason = 'win' | 'resign' | 'draw' | 'timeout'

// ==================== 游戏中的用户信息 ====================

/**
 * 游戏中的用户信息（简化版）
 * 注意: 与 user.types.ts 中的 UserInfo 不同，这是游戏相关的简化信息
 */
export interface GameUserInfo {
  id: string
  username: string
  avatar?: string
  rating?: number
}

// ==================== 房间相关 ====================

/** 房间状态 */
export type RoomState = 'waiting_players' | 'game_in_progress' | 'game_paused' | 'game_finished'

/** 客户端房间信息（简化版） */
export interface Room {
  id: string
  name: string
  status: GameStatus
  host: GameUserInfo
  players: GameUserInfo[]
  maxPlayers: number
  gameType: string
  createdAt: string
}

/** 服务端房间响应（完整版） */
export interface ServerRoomResponse {
  room_id: string
  creator_id: string
  current_players: number
  max_players: number
  game_type: number
  state: number
  players: any[]
  created_at: number
  last_activity: number
  gomokuStats?: {
    gameDurationSeconds: number
    gameMode: number
    spectatorCount: number
    totalMoves: number
  }
}

/** 房间详情 */
export interface RoomDetail {
  id: string
  name: string
  status: GameStatus
  host: GameUserInfo
  players: GameUserInfo[] | PlayerInfo[]
  maxPlayers: number
  gameType: string
  createdAt: string
  boardState?: BoardState
  gameConfig?: GameConfig
  spectators?: SpectatorInfo
  timestamps?: {
    createdAt: number
    lastActivity: number
    serverTime: number
  }
}

/** 棋盘状态 */
export interface BoardState {
  board: number[][]
  currentPlayer: number
  gameResult: number
  lastMove: { row: number; col: number }
  totalMoves: number
  blackTimeLeft: number
  whiteTimeLeft: number
  canUndo: boolean
  gameMode: number
  incrementPerMove: number
  forbiddenPositions: Array<{ row: number; col: number }>
  moveHistory: Array<{ row: number; col: number; player: number }>
}

/** 游戏配置 */
export interface GameConfig {
  gameMode: number
  gameModeStr: string
  timeLimit: number
  incrementPerMove: number
  allowUndo: boolean
  allowSpectators: boolean
  maxSpectators: number
  rankingEnabled: boolean
}

/** 玩家信息 */
export interface PlayerInfo {
  id: string
  username: string
  pieceType: 'black' | 'white'
  ready: boolean
}

/** 观战者信息 */
export interface SpectatorInfo {
  count: number
  ids: string[]
  limit: number
}

// ==================== 请求类型 ====================

/** 创建房间请求 */
export interface CreateRoomRequest {
  creatorId: string
  config?: {
    gameMode?: 'freestyle' | 'renju' | 'swap2' | 'pro' | 'tournament' | number
    timeLimit?: number
    incrementPerMove?: number
    allowUndo?: boolean
    allowSpectators?: boolean
    maxSpectators?: number
    handicap?: number
  }
}

/** 加入房间请求 (HTTP API 使用 camelCase) */
export interface JoinRoomRequest {
  playerId: string    // 玩家ID（HTTP API 使用 camelCase）
  role?: 'player' | 'spectator'
  password?: string
}

/** 加入房间响应 */
export interface JoinRoomResponse {
  message: string
  roomId: string
  playerId: string
  role: 'player' | 'spectator'
  pieceType?: 'black' | 'white'
  roomInfo: {
    currentPlayers: number
    maxPlayers: number
    spectatorCount: number
    roomState: RoomState
  }
}

/** 离开房间请求 (HTTP API 使用 camelCase) */
export interface LeaveRoomRequest {
  playerId: string    // 玩家ID（HTTP API 使用 camelCase）
}

/** 开始游戏请求 (HTTP API 使用 camelCase) */
export interface StartGameRequest {
  playerId: string    // 玩家ID（HTTP API 使用 camelCase）
}

/** 开始游戏响应 */
export interface StartGameResponse {
  message: string
  roomId: string
  gameState: {
    board: BoardState
    config: GameConfig
    currentTurn: string
    playerPieces: Record<string, number>
  }
}

// ==================== 匹配相关 ====================

/** 匹配偏好 */
export interface MatchPreferences {
  minRating?: number
  maxRating?: number
  gameType?: string
}

/** 匹配结果 */
export interface MatchResult {
  roomId: string
  opponent: GameUserInfo
  color: PlayerColor
}

/** 房间过滤 */
export interface RoomFilter {
  status?: GameStatus
  gameType?: string
  hasOpenSlots?: boolean
  offset?: number
  limit?: number
}

// ==================== 排行榜 ====================
// 注意: gamedata.types.ts 中定义了更完整的 LeaderboardType 和 LeaderboardEntry
// 这里的类型是旧版本，为避免冲突，重命名为 GameTypesLeaderboard*

/**
 * @deprecated 请使用 gamedata.types.ts 中的 LeaderboardType
 */
export interface GameTypesLeaderboardType {
  type: 'rating' | 'wins' | 'winRate'
  gameType?: string
}

/**
 * @deprecated 请使用 gamedata.types.ts 中的 LeaderboardData
 */
export interface GameTypesLeaderboardResponse {
  type: string
  leaderboard: GameTypesLeaderboardEntry[]
  userRank?: number
}

/**
 * 排行榜条目
 * @deprecated 请使用 gamedata.types.ts 中的 LeaderboardEntry
 */
export interface GameTypesLeaderboardEntry {
  rank: number
  playerId: string
  playerName: string
  rating: number
  wins: number
  losses: number
  draws: number
  winRate: number
  gameMode: string
}

/**
 * @deprecated 请使用 gamedata.types.ts 中的 UserRankData
 */
export interface GameTypesUserRank {
  rank: number
  rating: number
}

// ==================== 游戏统计 ====================

/** 游戏统计 */
export interface GameStats {
  activeRooms: number
  activePlayers: number
  totalGamesFinished: number
  totalGamesAbandoned: number
  averageGameDuration: number
  peakConcurrentPlayers: number
}

/** 服务器状态 */
export interface ServerStatus {
  server_name: string
  version: string
  uptime_seconds: number
  running: boolean
  architecture: {
    mode: string
    threads: {
      total_threads: number
      worker_threads: number
      io_threads: number
      event_loop_threads: number
    }
    event_system: string
    kafka_enabled: boolean
    monitoring_threads: number
  }
  config: {
    host: string
    port: number
    websocket_port: number
    max_concurrent_games: number
    allow_spectators: boolean
    enable_ranking: boolean
  }
  current_stats: {
    gomokuStats: GameStats
  }
  features: {
    game_modes: string[]
    room_types: string[]
    websocket: boolean
    leaderboard: boolean
    spectator_mode: boolean
    replay_system: boolean
    simplified_architecture: boolean
    unified_thread_pool: boolean
  }
  capacity: {
    max_rooms: number
    max_concurrent_games: number
    current_load: number
  }
}

// ==================== 游戏相关 ====================

export interface Game {
  id: string
  roomId: string
  status: GameStatus
  board: (PlayerColor | null)[][]
  currentPlayer: PlayerColor
  moves: Move[]
  players: {
    black: GameUserInfo | null
    white: GameUserInfo | null
  }
  createdAt: string
  updatedAt: string
}

export interface Move {
  position: Position
  color: PlayerColor
  timestamp: number
}
