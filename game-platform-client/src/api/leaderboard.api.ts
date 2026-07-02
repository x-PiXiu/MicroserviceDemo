/**
 * 排行榜 API
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md
 * - §4.1 排行榜类型 (LeaderboardType)
 * - §4.2 排行榜范围 (LeaderboardScope)
 * - §4.3 API 接口
 *
 * 基础路径: /leaderboard
 * 完整路径格式: /leaderboard/{leaderboard_type}/{game_type}?scope={scope}
 */
import { gameDataRequest as http } from './request'
import type {
  LeaderboardData,
  LeaderboardType,
  LeaderboardScope,
  UserRankData,
  GameDataGameType,
  // 旧类型（向后兼容）
  GameDataLeaderboardData,
  GameDataUserRankData,
  GameDataLeaderboardType,
} from '@/types'

/**
 * 排行榜类型配置
 */
export const LEADERBOARD_TYPE_CONFIG: Record<string, { label: string; icon: string }> = {
  rating: { label: '评分榜', icon: '🏆' },
  win_streak: { label: '连胜榜', icon: '🔥' },
  playtime: { label: '时长榜', icon: '⏱️' },
  win_rate: { label: '胜率榜', icon: '📊' },
  experience: { label: '经验榜', icon: '⭐' },
  achievements: { label: '成就榜', icon: '🎖️' },
}

/**
 * 排行榜范围配置
 */
export const LEADERBOARD_SCOPE_CONFIG: Record<string, { label: string }> = {
  global: { label: '全球' },
  weekly: { label: '周榜' },
  monthly: { label: '月榜' },
  seasonal: { label: '赛季' },
}

export const leaderboardApi = {
  /**
   * 获取排行榜（新版本 - 符合文档规范）
   * GET /leaderboard/{leaderboard_type}/{game_type}
   *
   * 文档依据: §4.3.1 获取排行榜
   *
   * @param type - 排行榜类型: 'rating' | 'win_streak' | 'playtime' | 'win_rate' | 'experience' | 'achievements'
   * @param gameType - 游戏类型: 'gomoku' | 'snake'
   * @param options - 可选参数
   * @param options.scope - 排行榜范围: 'global' | 'weekly' | 'monthly' | 'seasonal' (默认 global)
   * @param options.offset - 偏移量 (默认 0)
   * @param options.limit - 数量限制 (默认 50)
   * @param options.userId - 用户ID，用于获取 my_rank
   */
  getLeaderboard: (
    type: LeaderboardType = 'rating',
    gameType: GameDataGameType = 'gomoku',
    options?: {
      scope?: LeaderboardScope
      offset?: number
      limit?: number
      userId?: string
    }
  ) => {
    const params: Record<string, string | number> = {}
    if (options?.scope) params.scope = options.scope
    if (options?.offset !== undefined) params.offset = options.offset
    if (options?.limit !== undefined) params.limit = options.limit
    if (options?.userId) params.user_id = options.userId

    return http.get<LeaderboardData>(
      `/leaderboard/${type}/${gameType}`,
      { params }
    )
  },

  /**
   * 获取用户排名
   * GET /leaderboard/{leaderboard_type}/{game_type}/rank/{user_id}
   *
   * 文档依据: §4.3.2 获取用户排名
   *
   * @param type - 排行榜类型
   * @param gameType - 游戏类型
   * @param userId - 用户ID
   * @param scope - 排行榜范围 (默认 global)
   */
  getUserRank: (
    type: LeaderboardType,
    gameType: GameDataGameType,
    userId: string,
    scope: LeaderboardScope = 'global'
  ) =>
    http.get<UserRankData>(
      `/leaderboard/${type}/${gameType}/rank/${userId}`,
      { params: { scope } }
    ),

  // ==================== 便捷方法 ====================

  /**
   * 获取五子棋评分全球排行榜
   */
  getGomokuRatingLeaderboard: (options?: {
    scope?: LeaderboardScope
    limit?: number
    userId?: string
  }) =>
    leaderboardApi.getLeaderboard('rating', 'gomoku', options),

  /**
   * 获取五子棋连胜排行榜
   */
  getGomokuWinStreakLeaderboard: (options?: {
    scope?: LeaderboardScope
    limit?: number
    userId?: string
  }) =>
    leaderboardApi.getLeaderboard('win_streak', 'gomoku', options),

  /**
   * 获取五子棋胜率排行榜
   */
  getGomokuWinRateLeaderboard: (options?: {
    scope?: LeaderboardScope
    limit?: number
    userId?: string
  }) =>
    leaderboardApi.getLeaderboard('win_rate', 'gomoku', options),

  /**
   * 获取用户五子棋排名
   */
  getGomokuUserRank: (userId: string, type: LeaderboardType = 'rating', scope: LeaderboardScope = 'global') =>
    leaderboardApi.getUserRank(type, 'gomoku', userId, scope),

  // ==================== 旧版本 API（向后兼容，标记为废弃） ====================

  /**
   * 获取排行榜（旧版本 - 已废弃）
   * @deprecated 请使用 getLeaderboard(type, gameType, options) 替代
   * @param leaderboardType - 排行榜范围（旧版误用作类型）: 'global' | 'weekly' | 'monthly'
   * @param gameType - 游戏类型
   * @param params - 可选参数
   */
  getLeaderboardLegacy: (
    leaderboardType: GameDataLeaderboardType = 'global',
    gameType: GameDataGameType = 'gomoku',
    params?: {
      limit?: number
      offset?: number
      include_user?: string
    }
  ) =>
    http.get<GameDataLeaderboardData>(
      `/leaderboard/${leaderboardType}/${gameType}`,
      { params }
    ),

  /**
   * 获取用户排名（旧版本 - 已废弃）
   * @deprecated 请使用 getUserRank(type, gameType, userId, scope) 替代
   */
  getUserRankLegacy: (
    userId: string,
    leaderboardType: GameDataLeaderboardType = 'global',
    gameType: GameDataGameType = 'gomoku'
  ) =>
    http.get<GameDataUserRankData>(
      `/leaderboard/${leaderboardType}/${gameType}/rank/${userId}`
    ),
}

// 为了向后兼容，导出旧版本的便捷方法
export const getGomokuGlobalLeaderboard = (limit?: number) =>
  leaderboardApi.getLeaderboard('rating', 'gomoku', { scope: 'global', limit })

export const getGomokuWeeklyLeaderboard = (limit?: number) =>
  leaderboardApi.getLeaderboard('rating', 'gomoku', { scope: 'weekly', limit })

export const getGomokuUserRank = (userId: string) =>
  leaderboardApi.getUserRank('rating', 'gomoku', userId, 'global')
