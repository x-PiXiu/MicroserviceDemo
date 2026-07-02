/**
 * Game Data Store
 * 管理用户的游戏数据：游戏档案、货币、成就、库存
 * 参考: Game_Data_Service_API_Documentation.md
 *
 * 注意: HTTP 拦截器已提取 data，所以 response 直接是数据本身，不需要 response.data
 */
import { defineStore } from 'pinia'
import { ref, computed, readonly } from 'vue'
import { gameDataApi, loadUserGameData } from '@/api/gamedata.api'
import { useAuthStore } from './auth.store'
import { useAchievementStore } from './achievement.store'
import type {
  GameProfile,
  Currency,
  CurrencyType,
  GameDataAchievement,
  InventoryItem,
  UpdateGameProfileRequest,
  UseInventoryItemRequest,
  LeaderboardType,
  LeaderboardScope,
  GameDataGameType,
  // 旧类型（向后兼容）
  GameDataLeaderboardType,
} from '@/types'
import { normalizeCurrencyType } from '@/types/gamedata.types'

export const useGameDataStore = defineStore('gameData', () => {
  // ==================== State ====================

  const gameProfile = ref<GameProfile | null>(null)
  const currencies = ref<Currency[]>([])
  const achievements = ref<GameDataAchievement[]>([])
  const inventory = ref<InventoryItem[]>([])
  const loading = ref(false)
  const initialized = ref(false)

  // ==================== Getters ====================

  /**
   * 获取特定货币余额
   * 支持大小写不敏感的货币类型匹配
   */
  const getCurrencyBalance = computed(() => {
    return (type: CurrencyType | string): number => {
      // 标准化查询类型（处理大小写）
      const normalizedType = type.toLowerCase()
      // 查找时也进行大小写不敏感比较
      const currency = currencies.value.find((c) =>
        c.currency_type.toLowerCase() === normalizedType ||
        normalizeCurrencyType(c.currency_type) === normalizedType
      )
      return currency?.amount || 0
    }
  })

  /**
   * 金币余额（文档标准名称）
   */
  const gold = computed(() => getCurrencyBalance.value('gold'))

  /**
   * 宝石余额（文档标准名称）
   */
  const gem = computed(() => getCurrencyBalance.value('gem'))

  /**
   * 荣誉值余额（文档标准名称）
   */
  const honor = computed(() => getCurrencyBalance.value('honor'))

  // ===== 向后兼容的 getter =====

  /**
   * 金币余额（旧名称）
   * @deprecated 请使用 gold 替代
   */
  const coins = computed(() => gold.value)

  /**
   * 宝石余额（旧名称，使用复数形式）
   * @deprecated 请使用 gem 替代
   */
  const gems = computed(() => gem.value)

  /**
   * 代币余额（旧名称）
   * @deprecated 请使用 honor 替代
   */
  const tokens = computed(() => honor.value)

  /**
   * 用户等级
   */
  const level = computed(() => gameProfile.value?.level || 1)

  /**
   * 用户经验值
   */
  const experience = computed(() => gameProfile.value?.experience_points || 0)

  /**
   * 用户显示名称
   */
  const displayName = computed(() => gameProfile.value?.display_name || '')

  /**
   * 成就总积分
   */
  const totalAchievementPoints = computed(() =>
    achievements.value.reduce((sum, a) => sum + a.points, 0)
  )

  /**
   * 成就总数
   */
  const totalAchievements = computed(() => achievements.value.length)

  // ==================== Actions ====================

  /**
   * 加载所有游戏数据（登录后调用）
   * v2.0.0: 添加 gameType 参数，在选择游戏时加载
   * 如果用户档案不存在，会自动创建
   * @param userId 用户ID
   * @param displayName 显示名称（创建新档案时使用，可选）
   * @param gameType 游戏类型（v2.0.0 新增，获取特定游戏的数据）
   */
  const loadAllGameData = async (userId: string, displayName?: string, gameType?: string) => {
    loading.value = true
    try {
      const data = await loadUserGameData(userId, displayName, gameType)

      gameProfile.value = data.profile
      currencies.value = data.currencies
      achievements.value = data.achievements

      // 同步成就数据到 achievementStore，避免重复 API 调用
      const achievementStore = useAchievementStore()
      achievementStore.setAchievements(data.achievements)

      initialized.value = true
      return data
    } catch (error) {
      console.error('[GameDataStore] Failed to load game data:', error)
      throw error
    } finally {
      loading.value = false
    }
  }

  /**
   * 加载游戏档案
   * HTTP interceptor 已提取 data，返回 GameProfile
   * 如果档案不存在返回 null
   */
  const loadGameProfile = async (userId: string) => {
    try {
      const response = await gameDataApi.getGameProfile(userId)
      // response 已经是 GameProfile 类型（拦截器已提取 data）
      gameProfile.value = response || null
      return gameProfile.value
    } catch (error) {
      console.error('[GameDataStore] Failed to load game profile:', error)
      return null
    }
  }

  /**
   * 创建游戏档案（首次登录时调用）
   * HTTP interceptor 已提取 data，返回 GameProfile
   */
  const createGameProfile = async (userId: string, displayName: string) => {
    try {
      const response = await gameDataApi.createGameProfile({
        user_id: userId,
        display_name: displayName,
      })
      // response 已经是 GameProfile 类型（拦截器已提取 data）
      gameProfile.value = response || null
      return gameProfile.value
    } catch (error) {
      console.error('[GameDataStore] Failed to create game profile:', error)
      throw error
    }
  }

  /**
   * 加载货币
   * HTTP interceptor 已提取 data，返回 CurrencyListData
   */
  const loadCurrencies = async (userId: string) => {
    try {
      const response = await gameDataApi.getAllCurrencies(userId)
      // response 已经是 CurrencyListData 类型（拦截器已提取 data）
      currencies.value = response?.currencies || []
      return currencies.value
    } catch (error) {
      console.error('[GameDataStore] Failed to load currencies:', error)
      return []
    }
  }

  /**
   * 加载成就
   * HTTP interceptor 已提取 data，返回 GameDataAchievementListData
   */
  const loadAchievements = async (userId: string) => {
    try {
      const response = await gameDataApi.getAchievements(userId)
      // response 已经是 GameDataAchievementListData 类型（拦截器已提取 data）
      achievements.value = response?.items || []
      return achievements.value
    } catch (error) {
      console.error('[GameDataStore] Failed to load achievements:', error)
      return []
    }
  }

  /**
   * 加载库存
   * HTTP interceptor 已提取 data，返回 InventoryListData
   */
  const loadInventory = async (userId: string) => {
    try {
      const response = await gameDataApi.getInventory(userId)
      // response 已经是 InventoryListData 类型（拦截器已提取 data）
      inventory.value = response?.items || []
      return inventory.value
    } catch (error) {
      console.error('[GameDataStore] Failed to load inventory:', error)
      return []
    }
  }

  /**
   * 更新游戏档案
   * HTTP interceptor 已提取 data，返回 GameProfile
   */
  const updateGameProfile = async (userId: string, data: UpdateGameProfileRequest) => {
    loading.value = true
    try {
      const response = await gameDataApi.updateGameProfile(userId, data)
      // response 已经是 GameProfile 类型（拦截器已提取 data）
      gameProfile.value = response || null
      return gameProfile.value
    } finally {
      loading.value = false
    }
  }

  /**
   * 使用库存物品
   */
  const useInventoryItem = async (itemId: string, quantity: number = 1) => {
    const authStore = useAuthStore()
    const userId = authStore.user?.id
    if (!userId) {
      throw new Error('用户未登录')
    }

    try {
      const requestData: UseInventoryItemRequest = {
        item_id: itemId,
        quantity,
      }
      await gameDataApi.useInventoryItem(userId, requestData)
      // 重新加载库存
      await loadInventory(userId)
    } catch (error) {
      console.error('[GameDataStore] Failed to use item:', error)
      throw error
    }
  }

  /**
   * 获取排行榜（新版本）
   * 支持按类型和范围查询
   */
  const fetchLeaderboard = async (
    type: LeaderboardType = 'rating',
    gameType: GameDataGameType = 'gomoku',
    options?: {
      scope?: LeaderboardScope
      limit?: number
      userId?: string
    }
  ) => {
    try {
      const response = await gameDataApi.getLeaderboardNew(type, gameType, options)
      return response
    } catch (error) {
      console.error('[GameDataStore] Failed to fetch leaderboard:', error)
      return null
    }
  }

  /**
   * 获取用户排名（新版本）
   */
  const fetchUserRank = async (
    type: LeaderboardType,
    gameType: GameDataGameType,
    userId: string,
    scope: LeaderboardScope = 'global'
  ) => {
    try {
      const response = await gameDataApi.getUserRankNew(type, gameType, userId, scope)
      return response
    } catch (error) {
      console.error('[GameDataStore] Failed to fetch user rank:', error)
      return null
    }
  }

  /**
   * 获取排行榜（旧版本 - 向后兼容）
   * @deprecated 请使用 fetchLeaderboard 替代
   * HTTP interceptor 已提取 data，返回 GameDataLeaderboardData
   */
  const getLeaderboard = async (
    leaderboardType: GameDataLeaderboardType,
    gameType: GameDataGameType,
    limit?: number
  ) => {
    try {
      const response = await gameDataApi.getLeaderboard(leaderboardType, gameType, {
        limit: limit || 50,
      })
      // response 已经是 GameDataLeaderboardData 类型（拦截器已提取 data）
      return response
    } catch (error) {
      console.error('[GameDataStore] Failed to get leaderboard:', error)
      return null
    }
  }

  /**
   * 获取用户排名（旧版本 - 向后兼容）
   * @deprecated 请使用 fetchUserRank 替代
   * HTTP interceptor 已提取 data，返回 GameDataUserRankData
   */
  const getUserRank = async (
    leaderboardType: GameDataLeaderboardType,
    gameType: GameDataGameType,
    userId: string
  ) => {
    try {
      const response = await gameDataApi.getUserRank(leaderboardType, gameType, userId)
      // response 已经是 GameDataUserRankData 类型（拦截器已提取 data）
      return response
    } catch (error) {
      console.error('[GameDataStore] Failed to get user rank:', error)
      return null
    }
  }

  /**
   * 清除所有数据（登出时调用）
   */
  const clearData = () => {
    gameProfile.value = null
    currencies.value = []
    achievements.value = []
    inventory.value = []
    initialized.value = false
  }

  // ==================== 实时更新方法（WebSocket推送） ====================

  /**
   * 实时更新货币余额（由 currency_update 消息触发）
   * 不需要重新从服务器加载，直接更新本地状态
   */
  const updateCurrencyRealtime = (
    currencyType: CurrencyType,
    newAmount: number,
    change?: number
  ) => {
    const index = currencies.value.findIndex((c) => c.currency_type === currencyType)
    if (index >= 0) {
      // 更新现有货币
      currencies.value = currencies.value.map((c, i) =>
        i === index ? { ...c, amount: newAmount } : c
      )
    } else {
      // 添加新货币类型
      currencies.value = [
        ...currencies.value,
        {
          currency_type: currencyType,
          amount: newAmount,
        } as Currency,
      ]
    }
    console.log(
      '[GameDataStore] Currency updated:',
      currencyType,
      'new amount:',
      newAmount,
      change !== undefined ? `change: ${change}` : ''
    )
  }

  /**
   * 批量更新货币余额（由 game_end 消息触发）
   * 一次性更新多种货币，避免多次触发响应式更新
   */
  const updateCurrenciesBatch = (updates: Array<{
    currencyType: CurrencyType
    newAmount: number
    change?: number
  }>) => {
    updates.forEach(({ currencyType, newAmount }) => {
      const index = currencies.value.findIndex((c) => c.currency_type === currencyType)
      if (index >= 0) {
        currencies.value = currencies.value.map((c, i) =>
          i === index ? { ...c, amount: newAmount } : c
        )
      } else {
        currencies.value = [
          ...currencies.value,
          {
            currency_type: currencyType,
            amount: newAmount,
          } as Currency,
        ]
      }
    })
    console.log('[GameDataStore] Currencies batch updated:', updates.length, 'items')
  }

  return {
    // State
    gameProfile: readonly(gameProfile),
    currencies: readonly(currencies),
    achievements: readonly(achievements),
    inventory: readonly(inventory),
    loading: readonly(loading),
    initialized: readonly(initialized),

    // Getters
    getCurrencyBalance,
    // 新的文档标准 getter
    gold,
    gem,
    honor,
    // 向后兼容的 getter（已废弃）
    coins,
    gems,
    tokens,
    level,
    experience,
    displayName,
    totalAchievementPoints,
    totalAchievements,

    // Actions
    loadAllGameData,
    loadGameProfile,
    createGameProfile,
    loadCurrencies,
    loadAchievements,
    loadInventory,
    updateGameProfile,
    useInventoryItem,
    // 新的排行榜 API（推荐）
    fetchLeaderboard,
    fetchUserRank,
    // 旧的排行榜 API（向后兼容）
    getLeaderboard,
    getUserRank,
    clearData,

    // 实时更新（WebSocket推送）
    updateCurrencyRealtime,
    updateCurrenciesBatch,
  }
})
