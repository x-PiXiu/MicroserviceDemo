/**
 * 用户相关类型定义
 * 参考: User_Service_API_Documentation.md
 */

/**
 * 用户基础信息 (来自 GET /api/v1/user/:user_id)
 */
export interface UserInfo {
  user_id: string
  username: string
  email: string
  phone: string
  status: 'active' | 'inactive' | 'suspended' | 'banned' | 'deleted'
  online_status: 'offline' | 'online' | 'away' | 'busy'
  created_at: number
  updated_at: number
  last_login_ip: string
  login_attempts: number
}

/**
 * 更新用户基础信息请求
 */
export interface UpdateUserInfoRequest {
  email?: string
  phone?: string
}

/**
 * 用户档案 (来自 GET /api/v1/user/:user_id/profile)
 */
export interface UserProfile {
  user_id: string
  nickname: string
  real_name?: string
  avatar_url?: string
  bio?: string
  location?: string
  website?: string
  birth_date?: number
  gender?: 'male' | 'female' | 'other' | 'unknown'
  language?: string
  timezone?: string
  created_at: number
  updated_at: number
}

/**
 * 更新用户档案请求
 */
export interface UpdateProfileRequest {
  nickname?: string
  real_name?: string
  avatar_url?: string
  bio?: string
  location?: string
  website?: string
  birth_date?: number
  gender?: 'male' | 'female' | 'other' | 'unknown'
  language?: string
  timezone?: string
}

/**
 * 用户偏好设置 (来自 GET /api/v1/user/:user_id/preferences)
 */
export interface UserPreferences {
  user_id: string
  email_notifications: boolean
  push_notifications: boolean
  sms_notifications: boolean
  theme: 'light' | 'dark'
  language: string
  sound_effects: boolean
  music: boolean
  volume: number
  auto_login: boolean
  remember_me: boolean
  session_timeout: number
  custom_settings?: Record<string, unknown>
  created_at: number
  updated_at: number
}

/**
 * 更新用户偏好设置请求
 */
export interface UpdatePreferencesRequest {
  email_notifications?: boolean
  push_notifications?: boolean
  sms_notifications?: boolean
  theme?: 'light' | 'dark'
  language?: string
  sound_effects?: boolean
  music?: boolean
  volume?: number
  auto_login?: boolean
  remember_me?: boolean
  session_timeout?: number
  custom_settings?: Record<string, unknown>
}

/**
 * 用户名/邮箱检查响应
 */
export interface CheckAvailabilityResponse {
  username?: string
  email?: string
  exists: boolean
  available: boolean
}

/**
 * 在线状态更新响应
 */
export interface OnlineStatusUpdateResponse {
  user_id: string
  online_status: string
  message: string
}

/**
 * 文件上传响应
 */
export interface UploadResponse {
  url: string
  filename: string
}

// ==================== 兼容旧代码的类型 (逐步迁移后删除) ====================

/**
 * @deprecated 使用 UserInfo 代替
 */
export type UserStats = {
  userId: string
  totalGames: number
  wins: number
  losses: number
  draws: number
  winRate: number
  rating: number
  highestRating: number
}
