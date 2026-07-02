import { gomokuRequest as http } from './request'
import type {
  Room,
  RoomDetail,
  CreateRoomRequest,
  JoinRoomRequest,
  JoinRoomResponse,
  LeaveRoomRequest,
  StartGameRequest,
  StartGameResponse,
  MatchPreferences,
  MatchResult,
  RoomFilter,
  LeaderboardEntry,
  GameStats,
  ServerStatus,
  ServerRoomResponse,
} from '@/types'

/**
 * 将服务端房间数据转换为前端格式
 * 服务端: room_id, state, creator_id, current_players, created_at
 * 前端: id, status, host, players, createdAt
 */
function transformRoom(serverRoom: ServerRoomResponse): Room {
  // 状态映射: 0=waiting_players, 1=game_in_progress, 2=game_paused, 3=game_finished
  const stateToStatus = (state: number): 'waiting' | 'playing' | 'ended' => {
    switch (state) {
      case 0: return 'waiting'
      case 1: return 'playing'
      case 2: return 'playing'  // paused 仍然显示为 playing
      case 3: return 'ended'
      default: return 'waiting'
    }
  }

  // 游戏类型映射: 3=gomoku
  const gameTypeToName = (gameType: number): string => {
    switch (gameType) {
      case 3: return 'gomoku'
      default: return 'unknown'
    }
  }

  return {
    id: serverRoom.room_id,
    name: `房间 ${serverRoom.room_id.split('_')[1]}`,
    status: stateToStatus(serverRoom.state),
    host: {
      id: serverRoom.creator_id,
      username: serverRoom.creator_id.split('_')[1] || '未知',
    },
    players: (serverRoom.players || []).map((p: any) => ({
      id: p.id || p.player_id || '',
      username: p.username || p.name || '玩家',
    })),
    maxPlayers: serverRoom.max_players,
    gameType: gameTypeToName(serverRoom.game_type),
    createdAt: new Date(serverRoom.created_at).toISOString(),
  }
}

export const gomokuApi = {
  // ==================== 房间管理 ====================

  /** 获取房间列表 */
  getRooms: async (filter?: RoomFilter) => {
    const response = await http.get<{
      rooms: ServerRoomResponse[]
      count: number
      totalCount: number
      totalPages: number
      page: number
    }>('/rooms', { params: filter })

    // 转换房间数据格式
    return {
      ...response,
      rooms: response.rooms.map(transformRoom),
    }
  },

  /** 创建房间 */
  createRoom: (data: CreateRoomRequest) =>
    http.post<{ roomId: string; creatorId: string; message: string; config: any }>(
      '/rooms',
      data
    ),

  /** 获取房间详情 */
  getRoomDetail: async (roomId: string): Promise<RoomDetail> => {
    const response = await http.get<any>(`/rooms/${roomId}`)

    // 转换服务端响应为前端格式
    return {
      id: response.roomId,
      name: `房间 ${response.roomId?.split('_')[1] || ''}`,
      status: response.state === 0 ? 'waiting' :
              response.state === 3 ? 'ended' : 'playing',
      host: {
        id: response.creatorId || '',
        username: response.creatorId?.split('_')[1] || '房主',
      },
      players: (response.players || []).map((p: any) => ({
        id: p.id || p.playerId || '',
        username: p.username || p.name || '玩家',
        pieceType: p.pieceType || p.piece_type,
        ready: p.ready ?? false,
      })),
      maxPlayers: response.maxPlayers || 2,
      gameType: 'gomoku',
      createdAt: response.timestamps?.createdAt
        ? new Date(response.timestamps.createdAt).toISOString()
        : new Date().toISOString(),
      boardState: response.boardState,
      gameConfig: response.gameConfig,
      spectators: response.spectators,
      timestamps: response.timestamps,
    }
  },

  /** 加入房间 */
  joinRoom: (roomId: string, data: JoinRoomRequest) =>
    http.post<JoinRoomResponse>(`/rooms/${roomId}/join`, data),

  /** 离开房间 */
  leaveRoom: (roomId: string, data: LeaveRoomRequest) =>
    http.post<{ message: string; playerId: string; roomId: string }>(
      `/rooms/${roomId}/leave`,
      data
    ),

  /** 开始游戏 - 必须是房主且所有玩家已准备 */
  startGame: (roomId: string, data: StartGameRequest) =>
    http.post<StartGameResponse>(`/rooms/${roomId}/start`, data),

  // ==================== 快速匹配 ====================

  /** 快速匹配 */
  quickMatch: (preferences: MatchPreferences) =>
    http.post<MatchResult>('/match', preferences),

  // ==================== 排行榜与统计 ====================

  /** 获取排行榜 */
  getLeaderboard: (params?: { gameMode?: string; limit?: number }) =>
    http.get<{
      leaderboard: LeaderboardEntry[]
      count: number
      gameMode: string
      timestamp: number
    }>('/leaderboard', { params }),

  /** 获取游戏统计 */
  getStats: () =>
    http.get<{
      gomokuStats: GameStats
      serverUptime: number
      lastUpdated: number
    }>('/stats'),

  // ==================== 服务器状态 ====================

  /** 获取服务器状态 */
  getServerStatus: () => http.get<ServerStatus>('/server/status'),

  /** 健康检查 */
  healthCheck: () =>
    http.get<{ status: string; service: string; version: string; timestamp: number }>(
      '/health'
    ),
}
