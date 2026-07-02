/**
 * 成就系统 API
 * 参考: Game_Data_Service_API_Documentation.md
 *
 * 注意: 成就系统现在是 Game Data Service 的一部分
 * 基础路径: /achievements
 */
import { gameDataRequest as http } from './request'
import type { GameDataAchievement, GameDataAchievementListData } from '@/types'

export const achievementApi = {
  /**
   * 解锁成就
   * POST /achievements
   *
   * 注意: HTTP 拦截器会自动提取 response.data，所以返回类型直接是 GameDataAchievement
   */
  unlockAchievement: (data: {
    user_id: string
    achievement_type: string
    title: string
    description: string
    points: number
  }) =>
    http.post<GameDataAchievement>(
      '/achievements',
      data
    ),

  /**
   * 获取用户成就列表
   * GET /achievements/{user_id}
   *
   * 注意: HTTP 拦截器会自动提取 response.data，所以返回类型直接是 GameDataAchievementListData
   */
  getUserAchievements: (
    userId: string,
    params?: {
      achievement_type?: string
      sort_by?: string
      sort_order?: 'asc' | 'desc'
    }
  ) =>
    http.get<GameDataAchievementListData>(
      `/achievements/${userId}`,
      { params }
    ),

  /**
   * 获取当前用户成就（便捷方法）
   */
  getMyAchievements: (params?: {
    achievement_type?: string
    sort_by?: string
    sort_order?: 'asc' | 'desc'
  }) => {
    // 需要从 auth store 获取当前用户 ID
    // 这里返回一个需要 userId 的函数
    const userId = localStorage.getItem('user_id')
    if (!userId) {
      return Promise.reject(new Error('用户未登录'))
    }
    return achievementApi.getUserAchievements(userId, params)
  },

  /**
   * 获取用户成就积分
   * 通过获取成就列表后计算
   */
  getAchievementPoints: async (userId: string) => {
    const response = await achievementApi.getUserAchievements(userId)
    // response 是 GameDataAchievementListData 类型（HTTP 拦截器已提取 data）
    const totalPoints = response.items?.reduce((sum, a) => sum + a.points, 0) || 0
    return {
      totalPoints,
      totalAchievements: response.total_count || 0,
    }
  },
}
