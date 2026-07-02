/**
 * 成就系统 Store
 * 参考: Game_Data_Service_API_Documentation.md
 *
 * 成就数据现在由 Game Data Service 管理
 */
import { defineStore } from 'pinia'
import { ref, computed, readonly } from 'vue'
import { achievementApi } from '@/api'
import type { GameDataAchievement } from '@/types'

export const useAchievementStore = defineStore('achievement', () => {
  const achievements = ref<GameDataAchievement[]>([])
  const totalPoints = ref(0)
  const totalAchievements = ref(0)
  const loading = ref(false)
  const error = ref<string | null>(null)

  /**
   * 用户已解锁的成就数量
   */
  const unlockedCount = computed(() => achievements.value.length)

  /**
   * 按成就类型分组
   */
  const achievementsByType = computed(() => {
    const grouped: Record<string, GameDataAchievement[]> = {}
    achievements.value.forEach((achievement) => {
      const type = achievement.achievement_type
      if (!grouped[type]) {
        grouped[type] = []
      }
      grouped[type].push(achievement)
    })
    return grouped
  })

  /**
   * 获取用户成就列表
   */
  const getUserAchievements = async (
    userId: string,
    params?: {
      achievement_type?: string
      sort_by?: string
      sort_order?: 'asc' | 'desc'
    }
  ) => {
    loading.value = true
    error.value = null
    try {
      // HTTP 拦截器已提取 data，response 是 GameDataAchievementListData 类型
      const response = await achievementApi.getUserAchievements(userId, params)
      if (response) {
        achievements.value = response.items || []
        totalAchievements.value = response.total_count || 0
        totalPoints.value = achievements.value.reduce((sum, achievement) => sum + achievement.points, 0)
      }
      return achievements.value
    } catch (err: unknown) {
      const errorMessage = err instanceof Error ? err.message : '获取成就列表失败'
      error.value = errorMessage
      throw err
    } finally {
      loading.value = false
    }
  }

  /**
   * 解锁成就
   */
  const unlockAchievement = async (data: {
    user_id: string
    achievement_type: string
    title: string
    description: string
    points: number
  }) => {
    loading.value = true
    error.value = null
    try {
      // HTTP 拦截器已提取 data，response 是 GameDataAchievement 类型
      const response = await achievementApi.unlockAchievement(data)
      if (response) {
        // 添加新解锁的成就到列表
        const existingIndex = achievements.value.findIndex(
          (a) => a.achievement_id === response.achievement_id
        )
        if (existingIndex === -1) {
          achievements.value.push(response)
          totalPoints.value += response.points
          totalAchievements.value += 1
        }
      }
      return response
    } catch (err: unknown) {
      const errorMessage = err instanceof Error ? err.message : '解锁成就失败'
      error.value = errorMessage
      throw err
    } finally {
      loading.value = false
    }
  }

  /**
   * 由外部 store 注入成就数据（避免重复 API 调用）
   * gameDataStore.loadAllGameData 获取成就后调用此方法同步数据
   */
  const setAchievements = (items: GameDataAchievement[]) => {
    achievements.value = items
    totalAchievements.value = items.length
    totalPoints.value = items.reduce((sum, a) => sum + a.points, 0)
  }

  /**
   * 清空数据
   */
  const reset = () => {
    achievements.value = []
    totalPoints.value = 0
    totalAchievements.value = 0
    error.value = null
  }

  return {
    // State
    achievements,
    totalPoints,
    totalAchievements,
    loading: readonly(loading),
    error: readonly(error),

    // Getters
    unlockedCount,
    achievementsByType,

    // Actions
    getUserAchievements,
    unlockAchievement,
    setAchievements,
    reset,
  }
})
