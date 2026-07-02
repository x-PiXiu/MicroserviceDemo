import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { authApi } from '@/api'
import { storage, STORAGE_KEYS } from '@/utils'
import { logger } from '@/utils/logger'
import type { User, LoginRequest, RegisterRequest, GameSession } from '@/types'
import router from '@/router'

/**
 * 游戏服务器信息
 * v2.0.0: 使用服务发现 API 获取，而非游戏登录 API
 */
interface GameServerInfo {
  gameType: string
  host: string
  port: number
  wsPort: number
}

const parseJwt = (token: string) => {
  try {
    const base64Url = token.split('.')[1]
    const base64 = base64Url.replace(/-/g, '+').replace(/_/g, '/')
    const jsonPayload = decodeURIComponent(
      atob(base64)
        .split('')
        .map((c) => `%${('00' + c.charCodeAt(0).toString(16)).slice(-2)}`)
        .join('')
    )
    return JSON.parse(jsonPayload)
  } catch {
    return null
  }
}

export const useAuthStore = defineStore('auth', () => {
  // ==================== 用户认证状态 ====================
  const user = ref<User | null>(null)
  const accessToken = ref<string | null>(storage.get(STORAGE_KEYS.ACCESS_TOKEN))
  const refreshToken = ref<string | null>(storage.get(STORAGE_KEYS.REFRESH_TOKEN))
  const loading = ref(false)
  const error = ref<string | null>(null)

  // ==================== 游戏会话状态 ====================
  const gameSession = ref<GameSession | null>(null)

  // ==================== 游戏服务器信息 (v2.0.0) ====================
  const gameServer = ref<GameServerInfo | null>(null)

  // ==================== 计算属性 ====================
  const isAuthenticated = computed(() => !!accessToken.value && !!user.value)
  const userName = computed(() => user.value?.username || 'Guest')
  const isGameLoggedIn = computed(() => !!gameSession.value?.sessionToken)

  // ==================== 用户认证方法 ====================

  const setTokens = (access: string, refresh: string) => {
    accessToken.value = access
    refreshToken.value = refresh
    storage.set(STORAGE_KEYS.ACCESS_TOKEN, access)
    storage.set(STORAGE_KEYS.REFRESH_TOKEN, refresh)
  }

  const setUser = (userData: Partial<User>) => {
    const id = userData.id || user.value?.id || ''
    if (id || userData.username) {
      user.value = { ...user.value, ...userData, id } as User
      storage.set(STORAGE_KEYS.USER_INFO, user.value)
    }
  }

  const clearAuth = () => {
    user.value = null
    accessToken.value = null
    refreshToken.value = null
    gameSession.value = null
    gameServer.value = null
    storage.remove(STORAGE_KEYS.ACCESS_TOKEN)
    storage.remove(STORAGE_KEYS.REFRESH_TOKEN)
    storage.remove(STORAGE_KEYS.USER_INFO)
  }

  const login = async (credentials: LoginRequest) => {
    logger.auth.info('开始登录', credentials)
    logger.auth.debug('登录参数:', JSON.stringify(credentials))
    loading.value = true
    error.value = null
    
    try {
      const response = await authApi.login(credentials)
      logger.auth.info('登录响应成功:', JSON.stringify(response))
      logger.auth.debug('Access Token:', response.access_token?.substring(0, 50) + '...')
      
      setTokens(response.access_token, response.refresh_token)
      logger.auth.debug('Token已保存到storage')

      const jwtPayload = parseJwt(response.access_token)
      logger.auth.debug('JWT解析结果:', JSON.stringify(jwtPayload))
      const userId = jwtPayload?.sub || jwtPayload?.user_id || ''
      const username = jwtPayload?.username || credentials.username || credentials.email || ''

      logger.auth.debug('设置用户信息 - userId:', userId, 'username:', username)
      setUser({ id: userId, username })
      logger.auth.info('用户信息已设置，准备跳转到首页')
      
      logger.auth.debug('当前路由状态 - isAuthenticated:', isAuthenticated.value)
      logger.auth.debug('user.value:', user.value)
      logger.auth.debug('accessToken.value:', accessToken.value?.substring(0, 20) + '...')
      
      router.push('/')
      logger.auth.info('路由跳转已执行')
      return response
    } catch (err: unknown) {
      const errorMessage = err instanceof Error ? err.message : '登录失败'
      logger.auth.error('登录失败:', errorMessage, err)
      error.value = errorMessage
      throw err
    } finally {
      loading.value = false
      logger.auth.debug('loading状态已重置为false')
    }
  }

  const register = async (data: RegisterRequest) => {
    loading.value = true
    error.value = null
    try {
      const response = await authApi.register(data)
      setUser({ id: response.user_id, username: data.username, email: data.email })
      setTokens(response.access_token, response.refresh_token)
      return response
    } catch (err: unknown) {
      const errorMessage = err instanceof Error ? err.message : '注册失败'
      error.value = errorMessage
      throw err
    } finally {
      loading.value = false
    }
  }

  const logout = async () => {
    try {
      await authApi.logout()
    } catch {
      // ignore
    } finally {
      clearAuth()
      router.push('/login')
    }
  }

  const validateSession = async () => {
    if (!accessToken.value) return false

    try {
      const response = await authApi.validateToken()
      if (response.valid && response.user_id) {
        setUser({ id: response.user_id, username: response.username || '' })
        return true
      }
      return false
    } catch {
      return false
    }
  }

  const refreshAccessToken = async () => {
    if (!refreshToken.value) return false

    try {
      const response = await authApi.refreshToken(refreshToken.value)
      accessToken.value = response.access_token
      storage.set(STORAGE_KEYS.ACCESS_TOKEN, response.access_token)
      return true
    } catch {
      clearAuth()
      return false
    }
  }

  const loadUserFromStorage = () => {
    const savedUser = storage.get<User>(STORAGE_KEYS.USER_INFO)
    if (savedUser) {
      user.value = savedUser
    }
  }

  // ==================== 游戏会话方法 ====================

  /**
   * 游戏登录 - 获取游戏服务器信息和会话令牌
   * 在进入游戏前调用，用于建立 WebSocket 连接
   */
  const loginToGame = async (gameType: string) => {
    if (!isAuthenticated.value) {
      throw new Error('请先登录')
    }

    loading.value = true
    error.value = null
    try {
      const response = await authApi.gameLogin({ game_type: gameType })

      if (!response.success) {
        throw new Error('游戏登录失败')
      }

      // 保存游戏会话信息
      gameSession.value = {
        gameType,
        serverUrl: response.server_info.server_url,
        websocketUrl: response.server_info.websocket_url,
        sessionToken: response.session_token,
        expiresAt: response.session_expires_at,
        playerId: response.player_profile.player_id,
      }

      return gameSession.value
    } catch (err: unknown) {
      const errorMessage = err instanceof Error ? err.message : '游戏登录失败'
      error.value = errorMessage
      throw err
    } finally {
      loading.value = false
    }
  }

  /**
   * 清除游戏会话
   * 退出游戏时调用
   */
  const clearGameSession = () => {
    gameSession.value = null
  }

  /**
   * 检查游戏会话是否有效
   */
  const isGameSessionValid = () => {
    if (!gameSession.value) return false
    const expiresAt = new Date(gameSession.value.expiresAt).getTime()
    return Date.now() < expiresAt
  }

  // ==================== 游戏服务器方法 (v2.0.0) ====================

  /**
   * 设置游戏服务器信息
   * v2.0.0: 通过服务发现 API 获取，用于 WebSocket 连接
   */
  const setGameServer = (server: GameServerInfo) => {
    gameServer.value = server
    logger.auth.debug('Game server set:', server)
  }

  /**
   * 获取当前游戏服务器信息
   */
  const getGameServer = () => gameServer.value

  /**
   * 清除游戏服务器信息
   */
  const clearGameServer = () => {
    gameServer.value = null
  }

  return {
    // 用户认证状态
    user,
    accessToken,
    refreshToken,
    loading,
    error,

    // 游戏会话状态
    gameSession,

    // 游戏服务器信息 (v2.0.0)
    gameServer,

    // 计算属性
    isAuthenticated,
    userName,
    isGameLoggedIn,

    // 用户认证方法
    setTokens,
    setUser,
    clearAuth,
    login,
    register,
    logout,
    validateSession,
    refreshAccessToken,
    loadUserFromStorage,

    // 游戏会话方法
    loginToGame,
    clearGameSession,
    isGameSessionValid,

    // 游戏服务器方法 (v2.0.0)
    setGameServer,
    getGameServer,
    clearGameServer,
  }
})
