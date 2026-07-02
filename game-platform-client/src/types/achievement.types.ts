export interface Achievement {
  id: string
  name: string
  description: string
  icon: string
  rarity: 'common' | 'rare' | 'epic' | 'legendary'
  category: 'game' | 'social' | 'special'
  condition: string
  reward?: {
    type: 'badge' | 'title' | 'points'
    value: string | number
  }
  progress?: {
    current: number
    target: number
    unit: string
  }
  unlockedAt?: string
  isUnlocked: boolean
}

export interface UserAchievement {
  achievementId: string
  userId: string
  unlockedAt: string
  progress: number
  achievement: Achievement
}

export interface AchievementProgress {
  achievementId: string
  current: number
  target: number
  percentage: number
}

export interface AchievementNotification {
  achievement: Achievement
  unlockedAt: string
}
