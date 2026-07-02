/**
 * 用户 Store
 * 参考: User_Service_API_Documentation.md
 */
import { defineStore } from 'pinia'
import { ref, readonly } from 'vue'
import { userApi } from '@/api'
import { useAuthStore } from './auth.store'
import type {
  UserInfo,
  UserProfile,
  UpdateUserInfoRequest,
  UpdateProfileRequest,
  UserPreferences,
  UpdatePreferencesRequest,
} from '@/types/user.types'

export const useUserStore = defineStore('user', () => {
  // ==================== State ====================

  const userInfo = ref<UserInfo | null>(null)
  const profile = ref<UserProfile | null>(null)
  const preferences = ref<UserPreferences | null>(null)
  const loading = ref(false)

  // ==================== User Info Actions ====================

  /**
   * 获取用户基础信息
   * GET /api/v1/user/:user_id
   * HTTP interceptor 已提取 data，返回 UserInfo
   */
  const getUserInfo = async (userId: string) => {
    loading.value = true
    try {
      const response = await userApi.getUser(userId)
      // response 类型是 UserInfo（拦截器已提取 data）
      userInfo.value = response || null
      return userInfo.value
    } finally {
      loading.value = false
    }
  }

  /**
   * 更新用户基础信息 (email, phone)
   * PUT /api/v1/user/:user_id
   */
  const updateUserInfo = async (userId: string, data: UpdateUserInfoRequest) => {
    loading.value = true
    try {
      const response = await userApi.updateUser(userId, data)
      if (response) {
        userInfo.value = response
      }
      return userInfo.value
    } finally {
      loading.value = false
    }
  }

  // ==================== Profile Actions ====================

  /**
   * 获取用户档案
   * GET /api/v1/user/:user_id/profile
   * HTTP interceptor 已提取 data，返回 UserProfile
   */
  const getProfile = async (userId: string) => {
    loading.value = true
    try {
      const response = await userApi.getProfile(userId)
      // response 类型是 UserProfile（拦截器已提取 data）
      profile.value = response || null
      return profile.value
    } finally {
      loading.value = false
    }
  }

  /**
   * 更新用户档案
   * PUT /api/v1/user/:user_id/profile
   */
  const updateProfile = async (userId: string, data: UpdateProfileRequest) => {
    loading.value = true
    try {
      const response = await userApi.updateProfile(userId, data)
      profile.value = response || null
      return profile.value
    } finally {
      loading.value = false
    }
  }

  // ==================== Preferences Actions ====================

  /**
   * 获取用户偏好设置
   * GET /api/v1/user/:user_id/preferences
   */
  const getPreferences = async (userId: string) => {
    loading.value = true
    try {
      const response = await userApi.getPreferences(userId)
      preferences.value = response || null
      return preferences.value
    } finally {
      loading.value = false
    }
  }

  /**
   * 更新用户偏好设置
   * PUT /api/v1/user/:user_id/preferences
   */
  const updatePreferences = async (userId: string, prefs: UpdatePreferencesRequest) => {
    loading.value = true
    try {
      const response = await userApi.updatePreferences(userId, prefs)
      if (response) {
        preferences.value = response
      }
      return preferences.value
    } finally {
      loading.value = false
    }
  }

  // ==================== Online Status Actions ====================

  /**
   * 更新用户在线状态
   */
  const updateOnlineStatus = async (status: 'offline' | 'online' | 'away' | 'busy') => {
    const authStore = useAuthStore()
    const userId = authStore.user?.id
    if (!userId) {
      throw new Error('用户未登录')
    }

    loading.value = true
    try {
      await userApi.updateOnlineStatus(userId, status)
    } finally {
      loading.value = false
    }
  }

  // ==================== Utility Actions ====================

  /**
   * 清空数据
   */
  const reset = () => {
    userInfo.value = null
    profile.value = null
    preferences.value = null
  }

  return {
    // State
    userInfo,
    profile,
    preferences,
    loading: readonly(loading),

    // User Info Actions
    getUserInfo,
    updateUserInfo,

    // Profile Actions
    getProfile,
    updateProfile,

    // Preferences Actions
    getPreferences,
    updatePreferences,

    // Online Status Actions
    updateOnlineStatus,

    // Utility Actions
    reset,
  }
})
