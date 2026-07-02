/**
 * 用户服务 API
 * 参考: User_Service_API_Documentation.md
 *
 * 基础路径: /
 * 注意: user_service 是内部服务，外部客户端应通过 auth_service 进行认证操作
 *
 * HTTP 拦截器已处理响应，提取 data 字段，所以返回类型直接是 T 而不是 ApiResponse<T>
 */
import { userRequest as http } from './request'
import type {
  UserInfo,
  UpdateUserInfoRequest,
  UserProfile,
  UpdateProfileRequest,
  UserPreferences,
  UpdatePreferencesRequest,
  CheckAvailabilityResponse,
  OnlineStatusUpdateResponse,
} from '@/types/user.types'

export const userApi = {
  // ==================== 用户基础信息 API ====================

  /**
   * 获取用户基础信息
   * GET /:user_id
   */
  getUser: (userId: string) => http.get<UserInfo>(`/${userId}`),

  /**
   * 更新用户基础信息 (email, phone)
   * PUT /:user_id
   */
  updateUser: (userId: string, data: UpdateUserInfoRequest) =>
    http.put<UserInfo>(`/${userId}`, data),

  /**
   * 根据用户名获取用户
   * GET /by-username/:username
   */
  getUserByUsername: (username: string) =>
    http.get<UserInfo>(`/by-username/${encodeURIComponent(username)}`),

  /**
   * 根据邮箱获取用户
   * GET /by-email/:email
   */
  getUserByEmail: (email: string) =>
    http.get<UserInfo>(`/by-email/${encodeURIComponent(email)}`),

  // ==================== 用户档案 API ====================

  /**
   * 获取用户档案
   * GET /:user_id/profile
   */
  getProfile: (userId: string) =>
    http.get<UserProfile>(`/${userId}/profile`),

  /**
   * 更新用户档案
   * PUT /:user_id/profile
   */
  updateProfile: (userId: string, data: UpdateProfileRequest) =>
    http.put<UserProfile>(`/${userId}/profile`, data),

  // ==================== 用户偏好设置 API ====================

  /**
   * 获取用户偏好设置
   * GET /:user_id/preferences
   */
  getPreferences: (userId: string) =>
    http.get<UserPreferences>(`/${userId}/preferences`),

  /**
   * 更新用户偏好设置
   * PUT /:user_id/preferences
   */
  updatePreferences: (userId: string, prefs: UpdatePreferencesRequest) =>
    http.put<UserPreferences>(`/${userId}/preferences`, prefs),

  // ==================== 用户状态管理 API ====================

  /**
   * 更新用户在线状态
   * PUT /:user_id/online-status
   */
  updateOnlineStatus: (userId: string, status: 'offline' | 'online' | 'away' | 'busy') =>
    http.put<OnlineStatusUpdateResponse>(`/${userId}/online-status`, {
      online_status: status,
    }),

  // ==================== 用户验证 API ====================

  /**
   * 检查用户名是否存在
   * POST /check-username
   */
  checkUsername: (username: string) =>
    http.post<CheckAvailabilityResponse>('/check-username', { username }),

  /**
   * 检查邮箱是否存在
   * POST /check-email
   */
  checkEmail: (email: string) =>
    http.post<CheckAvailabilityResponse>('/check-email', { email }),

  // ==================== 系统管理 API ====================

  /**
   * 健康检查
   * GET /health
   */
  healthCheck: () =>
    http.get<{ status: string; service: string; version: string; timestamp: number }>(
      '/health'
    ),
}
