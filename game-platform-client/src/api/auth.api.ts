import { authRequest } from './request'
import type {
  LoginRequest,
  LoginResponse,
  RegisterRequest,
  RegisterResponse,
  RefreshResponse,
  ValidateResponse,
  GameLoginRequest,
  GameLoginResponse,
  SendVerifyCodeRequest,
  SendVerifyCodeResponse,
  VerifyCodeRequest,
  VerifyCodeResponse,
  ResetPasswordRequest,
  ResetPasswordResponse,
} from '@/types'

export const authApi = {
  // ==================== 用户认证 ====================

  login: (data: LoginRequest) => authRequest.post<LoginResponse>('/login', data),

  /**
   * 注册（支持邮箱验证码模式）
   * 当 email 提供了 verify_token 时使用邮箱验证码注册
   */
  register: (data: RegisterRequest) => authRequest.post<RegisterResponse>('/register', data),

  refreshToken: (refreshToken: string) =>
    authRequest.post<RefreshResponse>('/refresh', { refresh_token: refreshToken }),

  logout: () => authRequest.post('/logout'),

  validateToken: () => authRequest.get<ValidateResponse>('/validate'),

  // ==================== 邮箱验证码 ====================

  /**
   * 发送邮箱验证码
   * @param email 目标邮箱
   * @param purpose 用途: register | login | reset_password
   */
  sendVerifyCode: (data: SendVerifyCodeRequest) =>
    authRequest.post<SendVerifyCodeResponse>('/email/send-code', data),

  /**
   * 校验验证码
   * @param data 包含 email, code, purpose
   * @returns purpose=register 时返回 verify_token
   */
  verifyCode: (data: VerifyCodeRequest) =>
    authRequest.post<VerifyCodeResponse>('/email/verify-code', data),

  // ==================== 密码重置 ====================

  /**
   * 重置密码（通过邮箱验证码验证后）
   * @param data 包含 email, code, new_password
   */
  resetPassword: (data: ResetPasswordRequest) =>
    authRequest.post<ResetPasswordResponse>('/password/reset', data),

  // ==================== 游戏登录 ====================

  /**
   * 游戏登录 - 获取游戏服务器信息和会话令牌
   * 用于建立 WebSocket 连接前的认证
   */
  gameLogin: (data: GameLoginRequest) =>
    authRequest.post<GameLoginResponse>('/game/login', data),
}
