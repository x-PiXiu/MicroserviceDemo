/**
 * Game Data Service API
 * 参考: Game_Data_Service_API_Documentation.md
 *
 * 基础路径: /api/v1/gamedata/
 * 默认端口: 8084
 *
 * HTTP 拦截器已处理响应，提取 data 字段，所以返回类型直接是 T 而不是 ApiResponse<T>
 */
import { gameDataRequest } from './request'
import type {
  GameProfile,
  CreateGameProfileRequest,
  UpdateGameProfileRequest,
  GameProfileListData,
  Currency,
  CurrencyListData,
  CurrencyData,
  UpdateCurrencyRequest,
  UpdateCurrencyData,
  GameDataAchievement,
  GameDataAchievementListData,
  UnlockAchievementRequest,
  InventoryItem,
  AddInventoryItemRequest,
  UseInventoryItemRequest,
  UseInventoryItemData,
  InventoryListData,
  // 新的排行榜类型
  LeaderboardData,
  LeaderboardType,
  LeaderboardScope,
  UserRankData,
  UpdateLeaderboardRequest,
  GameDataGameType,
  // 每日统计
  DailyStats,
  // 旧类型（向后兼容）
  GameDataLeaderboardData,
  GameDataLeaderboardEntry,
  GameDataUserRankData,
  GameDataLeaderboardType,
} from '@/types'

// 正确的基础路径 (注意: 是 gamedata 不是 game-data)
// 注意: baseURL 已经在 gameDataRequest 中设置为 http://HOST:8084/api/v1/gamedata
const BASE_PATH = ''

export const gameDataApi = {
  // ==================== 用户游戏档案 API ====================

  /**
   * 创建用户游戏档案
   * POST /api/v1/gamedata/profiles
   * HTTP interceptor 已提取 data，返回 GameProfile
   */
  createGameProfile: (data: CreateGameProfileRequest) =>
    gameDataRequest.post<GameProfile>(`${BASE_PATH}/profiles`, data),

  /**
   * 获取用户游戏档案
   * GET /api/v1/gamedata/profiles/{user_id}
   * v2.0.0: 添加 game_type 查询参数，获取特定游戏的档案
   * HTTP interceptor 已提取 data，返回 GameProfile
   */
  getGameProfile: (userId: string, gameType?: string) =>
    gameDataRequest.get<GameProfile>(`${BASE_PATH}/profiles/${userId}`, {
      params: gameType ? { game_type: gameType } : undefined
    }),

  /**
   * 更新用户游戏档案
   * PUT /api/v1/gamedata/profiles/{user_id}
   * HTTP interceptor 已提取 data，返回 GameProfile
   */
  updateGameProfile: (userId: string, data: UpdateGameProfileRequest) =>
    gameDataRequest.put<GameProfile>(`${BASE_PATH}/profiles/${userId}`, data),

  /**
   * 删除用户游戏档案
   * DELETE /api/v1/gamedata/profiles/{user_id}
   */
  deleteGameProfile: (userId: string) =>
    gameDataRequest.delete<void>(`${BASE_PATH}/profiles/${userId}`),

  /**
   * 获取用户档案列表
   * GET /api/v1/gamedata/profiles
   * HTTP interceptor 已提取 data，返回 GameProfileListData
   */
  getGameProfileList: (params?: {
    page?: number
    limit?: number
    sort_by?: string
    sort_order?: 'asc' | 'desc'
    search?: string
    level_min?: number
    level_max?: number
  }) =>
    gameDataRequest.get<GameProfileListData>(`${BASE_PATH}/profiles`, { params }),

  // ==================== 货币系统 API ====================

  /**
   * 获取用户特定货币
   * GET /api/v1/gamedata/currency/{user_id}/{currency_type}
   * HTTP interceptor 已提取 data，返回 CurrencyData
   */
  getCurrency: (userId: string, currencyType: string) =>
    gameDataRequest.get<CurrencyData>(`${BASE_PATH}/currency/${userId}/${currencyType}`),

  /**
   * 更新用户货币
   * PUT /api/v1/gamedata/currency
   * HTTP interceptor 已提取 data，返回 UpdateCurrencyData
   */
  updateCurrency: (data: UpdateCurrencyRequest) =>
    gameDataRequest.put<UpdateCurrencyData>(`${BASE_PATH}/currency`, data),

  /**
   * 获取用户所有货币
   * GET /api/v1/gamedata/currency/{user_id}
   * HTTP interceptor 已提取 data，返回 CurrencyListData
   */
  getAllCurrencies: (userId: string) =>
    gameDataRequest.get<CurrencyListData>(`${BASE_PATH}/currency/${userId}`),

  // ==================== 成就系统 API ====================

  /**
   * 解锁成就
   * POST /api/v1/gamedata/achievements
   * HTTP interceptor 已提取 data，返回 GameDataAchievement
   */
  unlockAchievement: (data: UnlockAchievementRequest) =>
    gameDataRequest.post<GameDataAchievement>(`${BASE_PATH}/achievements`, data),

  /**
   * 获取用户成就
   * GET /api/v1/gamedata/achievements/{user_id}
   * HTTP interceptor 已提取 data，返回 GameDataAchievementListData
   */
  getAchievements: (
    userId: string,
    params?: {
      achievement_type?: string
      sort_by?: string
      sort_order?: 'asc' | 'desc'
    }
  ) =>
    gameDataRequest.get<GameDataAchievementListData>(`${BASE_PATH}/achievements/${userId}`, { params }),

  // ==================== 库存管理 API ====================

  /**
   * 添加库存物品
   * POST /api/v1/gamedata/inventory
   * HTTP interceptor 已提取 data，返回 InventoryItem
   */
  addInventoryItem: (data: AddInventoryItemRequest) =>
    gameDataRequest.post<InventoryItem>(`${BASE_PATH}/inventory`, data),

  /**
   * 获取用户库存
   * GET /api/v1/gamedata/inventory/{user_id}
   * HTTP interceptor 已提取 data，返回 InventoryListData
   */
  getInventory: (
    userId: string,
    params?: {
      item_type?: string
      sort_by?: string
      sort_order?: 'asc' | 'desc'
    }
  ) =>
    gameDataRequest.get<InventoryListData>(`${BASE_PATH}/inventory/${userId}`, { params }),

  /**
   * 使用库存物品
   * POST /api/v1/gamedata/inventory/{user_id}/use
   * HTTP interceptor 已提取 data，返回 UseInventoryItemData
   */
  useInventoryItem: (userId: string, data: UseInventoryItemRequest) =>
    gameDataRequest.post<UseInventoryItemData>(`${BASE_PATH}/inventory/${userId}/use`, data),

  // ==================== 排行榜 API ====================

  /**
   * 更新排行榜
   * POST /api/v1/gamedata/leaderboard
   * HTTP interceptor 已提取 data，返回 GameDataLeaderboardEntry
   */
  updateLeaderboard: (data: UpdateLeaderboardRequest) =>
    gameDataRequest.post<GameDataLeaderboardEntry>(`${BASE_PATH}/leaderboard`, data),

  /**
   * 获取排行榜（新版本 - 符合文档规范）
   * GET /api/v1/gamedata/leaderboard/{type}/{game_type}?scope={scope}
   *
   * 文档依据: client_economy_rating_leaderboard_integration_guide.md §4.3.1
   *
   * @param type - 排行榜类型
   * @param gameType - 游戏类型
   * @param options - 可选参数
   */
  getLeaderboardNew: (
    type: LeaderboardType,
    gameType: GameDataGameType,
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

    return gameDataRequest.get<LeaderboardData>(
      `${BASE_PATH}/leaderboard/${type}/${gameType}`,
      { params }
    )
  },

  /**
   * 获取用户排名（新版本 - 符合文档规范）
   * GET /api/v1/gamedata/leaderboard/{type}/{game_type}/rank/{user_id}?scope={scope}
   *
   * 文档依据: client_economy_rating_leaderboard_integration_guide.md §4.3.2
   */
  getUserRankNew: (
    type: LeaderboardType,
    gameType: GameDataGameType,
    userId: string,
    scope: LeaderboardScope = 'global'
  ) =>
    gameDataRequest.get<UserRankData>(
      `${BASE_PATH}/leaderboard/${type}/${gameType}/rank/${userId}`,
      { params: { scope } }
    ),

  // ===== 旧版本 API（向后兼容） =====

  /**
   * 获取排行榜（旧版本 - 已废弃）
   * @deprecated 请使用 getLeaderboardNew 替代
   * GET /api/v1/gamedata/leaderboard/{leaderboard_type}/{game_type}
   * HTTP interceptor 已提取 data，返回 GameDataLeaderboardData
   */
  getLeaderboard: (
    leaderboardType: GameDataLeaderboardType,
    gameType: GameDataGameType,
    params?: {
      limit?: number
      offset?: number
      include_user?: string
    }
  ) =>
    gameDataRequest.get<GameDataLeaderboardData>(
      `${BASE_PATH}/leaderboard/${leaderboardType}/${gameType}`,
      { params }
    ),

  /**
   * 获取用户排名（旧版本 - 已废弃）
   * @deprecated 请使用 getUserRankNew 替代
   * GET /api/v1/gamedata/leaderboard/{leaderboard_type}/{game_type}/rank/{user_id}
   * HTTP interceptor 已提取 data，返回 GameDataUserRankData
   */
  getUserRank: (
    leaderboardType: GameDataLeaderboardType,
    gameType: GameDataGameType,
    userId: string
  ) =>
    gameDataRequest.get<GameDataUserRankData>(
      `${BASE_PATH}/leaderboard/${leaderboardType}/${gameType}/rank/${userId}`
    ),

  // ==================== 统计数据 API ====================

  /**
   * 获取用户每日统计
   * GET /api/v1/gamedata/stats/daily/{user_id}
   *
   * 文档依据: client_economy_rating_leaderboard_integration_guide.md §10.2.2
   *
   * @param userId - 用户ID
   * @param params - 可选参数
   * @param params.game_type_id - 游戏类型ID（可选，默认 1 = gomoku）
   * @param params.date - 日期 YYYY-MM-DD（可选，默认今天）
   */
  getDailyStats: (
    userId: string,
    params?: {
      game_type_id?: number
      date?: string
    }
  ) =>
    gameDataRequest.get<DailyStats>(`${BASE_PATH}/stats/daily/${userId}`, { params }),

  // ==================== 系统管理 API ====================

  /**
   * 健康检查
   * GET /api/v1/gamedata/health
   */
  healthCheck: () =>
    gameDataRequest.get<{ status: string; service: string; version: string; timestamp: number }>(
      `${BASE_PATH}/health`
    ),
}

/**
 * 获取用户核心游戏数据（并行加载用）
 * v2.0.0: 添加 gameType 参数，获取特定游戏的数据
 * 注意: 如果用户档案不存在，会自动创建新档案
 * HTTP 拦截器已提取 data，所以 response 直接是数据本身
 */
export async function loadUserGameData(
  userId: string,
  displayName?: string,
  gameType?: string
): Promise<{
  profile: GameProfile | null
  currencies: Currency[]
  achievements: GameDataAchievement[]
}> {
  try {
    // 先尝试获取用户档案（带 game_type 参数）
    let profileRes = await gameDataApi.getGameProfile(userId, gameType).catch(async (err) => {
      // 如果是 404 错误，说明用户档案不存在，需要创建
      if (err?.response?.status === 404 || err?.message?.includes('404') || err?.message?.includes('not found')) {
        console.log('[loadUserGameData] 用户档案不存在，尝试创建新档案')

        try {
          // 创建新用户档案
          const newProfile = await gameDataApi.createGameProfile({
            user_id: userId,
            display_name: displayName || `Player_${userId.slice(-6)}`,
          })
          console.log('[loadUserGameData] 新用户档案创建成功')
          return newProfile
        } catch (createErr) {
          console.error('[loadUserGameData] 创建用户档案失败:', createErr)
          return null
        }
      }

      console.warn('[loadUserGameData] 获取用户档案失败:', err)
      return null
    })

    // 并行获取货币和成就
    const [currenciesRes, achievementsRes] = await Promise.all([
      gameDataApi.getAllCurrencies(userId).catch((err) => {
        console.warn('[loadUserGameData] 获取货币信息失败:', err)
        return null
      }),
      gameDataApi.getAchievements(userId).catch((err) => {
        console.warn('[loadUserGameData] 获取成就信息失败:', err)
        return null
      }),
    ])

    return {
      // 拦截器已提取 data，response 直接是数据本身
      profile: profileRes || null,
      currencies: currenciesRes?.currencies || [],
      achievements: achievementsRes?.items || [],
    }
  } catch (error) {
    console.error('[loadUserGameData] 加载用户游戏数据失败:', error)
    return {
      profile: null,
      currencies: [],
      achievements: [],
    }
  }
}
