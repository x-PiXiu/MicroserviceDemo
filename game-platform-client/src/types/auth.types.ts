export interface LoginRequest {
  username?: string
  password?: string
  email?: string
  code?: string
}

export interface RegisterRequest {
  username: string
  password: string
  email?: string
  verify_token?: string  // 邮箱验证 token（register 时需要）
}

export interface LoginResponse {
  access_token: string
  expires_in: number
  refresh_token: string
  success: boolean
  user_id?: string
  login_type?: 'password' | 'email_code'
}

export interface RegisterResponse {
  access_token: string
  expires_in: number
  refresh_token: string
  success: boolean
  message: string
  user_id: string
}

export interface RefreshResponse {
  access_token: string
}

export interface ValidateResponse {
  valid: boolean
  user_id?: string
  username?: string
}

export interface User {
  id: string
  username: string
  email?: string
  avatar?: string
}

// ==================== 邮箱验证码相关 ====================

/**
 * 发送验证码请求
 * purpose: register | login | reset_password
 */
export interface SendVerifyCodeRequest {
  email: string
  purpose: 'register' | 'login' | 'reset_password'
}

/**
 * 发送验证码响应
 */
export interface SendVerifyCodeResponse {
  success: boolean
  message: string
  cooldown_seconds?: number
}

/**
 * 校验验证码请求
 */
export interface VerifyCodeRequest {
  email: string
  code: string
  purpose: 'register' | 'login' | 'reset_password'
}

/**
 * 校验验证码响应（register 时返回 verify_token）
 */
export interface VerifyCodeResponse {
  success: boolean
  message: string
  verify_token?: string  // purpose=register 时返回
}

/**
 * 重置密码请求
 */
export interface ResetPasswordRequest {
  email: string
  code: string
  new_password: string
}

/**
 * 重置密码响应
 */
export interface ResetPasswordResponse {
  success: boolean
  message: string
  error?: string
}

// ==================== 游戏登录相关 ====================

/**
 * 游戏登录请求
 */
export interface GameLoginRequest {
  game_type: string
  player_id?: string
}

/**
 * 游戏服务器信息
 */
export interface GameServerInfo {
  server_id: string
  server_url: string
  websocket_url: string
  region: string
  load: number
}

/**
 * 游戏玩家档案（游戏登录返回）
 */
export interface GamePlayerProfile {
  player_id: string
  username: string
  level: number
  rating: number
  avatar?: string
}

/**
 * 游戏登录响应
 */
export interface GameLoginResponse {
  success: boolean
  server_info: GameServerInfo
  session_token: string
  session_expires_at: string
  player_profile: GamePlayerProfile
}

/**
 * 游戏会话（客户端状态管理用）
 */
export interface GameSession {
  gameType: string
  serverUrl: string
  websocketUrl: string
  sessionToken: string
  expiresAt: string
  playerId: string
}
