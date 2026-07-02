# 游戏平台客户端开发计划 (Vue 3)

## 文档信息

| 项目 | 内容 |
|------|------|
| 版本 | 1.0.0 |
| 创建日期 | 2026-02-19 |
| 技术栈 | Vue 3 + TypeScript + Vite |
| 目标平台 | Web (Desktop/Mobile) |

---

## 1. 项目概述

### 1.1 项目背景

基于现有微服务后端架构，开发一个现代化的游戏平台 Web 客户端。平台支持用户认证、游戏大厅、实时对战（五子棋）、排行榜、成就系统等核心功能。

### 1.2 技术选型

| 技术 | 版本 | 用途 |
|------|------|------|
| Vue 3 | 3.4+ | 核心框架 |
| TypeScript | 5.0+ | 类型安全 |
| Vite | 5.0+ | 构建工具 |
| Pinia | 2.1+ | 状态管理 |
| Vue Router | 4.2+ | 路由管理 |
| Axios | 1.6+ | HTTP 客户端 |
| Socket.io-client | 4.7+ | WebSocket 通信 |
| Element Plus | 2.4+ | UI 组件库 |
| TailwindCSS | 3.4+ | 样式框架 |
| VueUse | 10.0+ | 组合式工具集 |

### 1.3 后端服务对接

```
┌─────────────────────────────────────────────────────────────────┐
│                     Vue 3 Client Application                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │   HTTP API  │  │  WebSocket  │  │      Local Storage      │  │
│  │   (Axios)   │  │ (Socket.io) │  │    (Token/Prefs/Cache)  │  │
│  └──────┬──────┘  └──────┬──────┘  └─────────────────────────┘  │
│         │                │                                       │
└─────────┼────────────────┼───────────────────────────────────────┘
          │                │
          ▼                ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Nginx Reverse Proxy                         │
│                        (172.26.26.199:80)                        │
└─────────────────────────────────────────────────────────────────┘
          │                │
          ▼                ▼
┌─────────────────┐  ┌─────────────────┐
│  RESTful APIs   │  │   WebSocket     │
│                 │  │   /ws/gomoku/   │
└────────┬────────┘  └────────┬────────┘
         │                    │
    ┌────┴────┐          ┌────┴────┐
    ▼         ▼          ▼         │
┌───────┐ ┌───────┐  ┌─────────┐   │
│ Auth  │ │ User  │  │ Gomoku  │◄──┘
│ 8083  │ │ 8082  │  │   8085  │
└───────┘ └───────┘  └─────────┘
         │
    ┌────┴────┐
    ▼         ▼
┌───────┐ ┌───────┐
│ Game  │ │Service│
│ Data  │ │Reg.   │
│ 8084  │ │ 8090  │
└───────┘ └───────┘
```

---

## 2. 项目结构

### 2.1 目录结构

```
game-platform-client/
├── public/
│   ├── favicon.ico
│   ├── pwa-192x192.png          # PWA 图标
│   ├── pwa-512x512.png
│   └── assets/
│       ├── images/
│       └── sounds/
├── src/
│   ├── api/                    # API 层
│   │   ├── index.ts            # API 统一导出
│   │   ├── request.ts          # Axios 封装
│   │   ├── auth.api.ts         # 认证 API
│   │   ├── user.api.ts         # 用户 API
│   │   ├── game.api.ts         # 游戏 API
│   │   ├── gomoku.api.ts       # 五子棋 API
│   │   └── leaderboard.api.ts  # 排行榜 API
│   │
│   ├── assets/                 # 静态资源
│   │   ├── images/
│   │   ├── styles/
│   │   │   ├── main.css
│   │   │   ├── variables.css
│   │   │   └── themes.css      # 主题变量
│   │   └── fonts/
│   │
│   ├── components/             # 组件
│   │   ├── common/             # 通用组件
│   │   │   ├── AppHeader.vue
│   │   │   ├── AppFooter.vue
│   │   │   ├── AppSidebar.vue
│   │   │   ├── LoadingSpinner.vue
│   │   │   ├── ErrorBoundary.vue
│   │   │   ├── ModalDialog.vue
│   │   │   ├── ThemeSwitch.vue # 主题切换
│   │   │   └── OfflineBanner.vue # 离线提示
│   │   │
│   │   ├── mobile/             # 移动端组件
│   │   │   ├── MobileHeader.vue
│   │   │   ├── MobileNav.vue
│   │   │   └── BottomNavigation.vue
│   │   │
│   │   ├── auth/               # 认证组件
│   │   │   ├── LoginForm.vue
│   │   │   ├── RegisterForm.vue
│   │   │   └── PasswordReset.vue
│   │   │
│   │   ├── game/               # 游戏组件
│   │   │   ├── GameLobby.vue
│   │   │   ├── GameRoom.vue
│   │   │   ├── GameBoard.vue
│   │   │   ├── GamePlayer.vue
│   │   │   └── GameChat.vue
│   │   │
│   │   ├── gomoku/             # 五子棋组件
│   │   │   ├── GomokuBoard.vue
│   │   │   ├── GomokuCell.vue
│   │   │   ├── GomokuHistory.vue
│   │   │   └── GomokuTimer.vue
│   │   │
│   │   └── user/               # 用户组件
│   │       ├── UserProfile.vue
│   │       ├── UserStats.vue
│   │       ├── AchievementList.vue
│   │       └── AchievementBadge.vue
│   │
│   ├── composables/            # 组合式函数
│   │   ├── useAuth.ts          # 认证逻辑
│   │   ├── useWebSocket.ts     # WebSocket
│   │   ├── useGame.ts          # 游戏逻辑
│   │   ├── useNotification.ts  # 通知
│   │   ├── useStorage.ts       # 本地存储
│   │   ├── useBreakpoint.ts    # 响应式断点
│   │   ├── useDevice.ts        # 设备检测
│   │   ├── useGestures.ts      # 触摸手势
│   │   ├── useTheme.ts         # 主题切换
│   │   ├── useOffline.ts       # 离线状态
│   │   └── useSafeArea.ts      # 安全区域
│   │
│   ├── layouts/                # 布局组件
│   │   ├── DefaultLayout.vue
│   │   ├── GameLayout.vue
│   │   ├── AuthLayout.vue
│   │   └── ResponsiveLayout.vue # 响应式布局
│   │
│   ├── locales/                # 国际化
│   │   ├── index.ts            # i18n 配置
│   │   ├── zh-CN/              # 简体中文
│   │   │   ├── common.json
│   │   │   ├── auth.json
│   │   │   ├── game.json
│   │   │   └── user.json
│   │   └── en-US/              # 英文
│   │       ├── common.json
│   │       ├── auth.json
│   │       ├── game.json
│   │       └── user.json
│   │
│   ├── router/                 # 路由
│   │   ├── index.ts
│   │   ├── guards.ts
│   │   └── routes/
│   │       ├── auth.routes.ts
│   │       ├── game.routes.ts
│   │       └── user.routes.ts
│   │
│   ├── stores/                 # 状态管理
│   │   ├── index.ts
│   │   ├── auth.store.ts
│   │   ├── user.store.ts
│   │   ├── game.store.ts
│   │   └── notification.store.ts
│   │
│   ├── types/                  # TypeScript 类型
│   │   ├── api.types.ts
│   │   ├── auth.types.ts
│   │   ├── user.types.ts
│   │   ├── game.types.ts
│   │   └── websocket.types.ts
│   │
│   ├── utils/                  # 工具函数
│   │   ├── date.ts
│   │   ├── storage.ts
│   │   ├── validator.ts
│   │   ├── constants.ts
│   │   ├── sanitize.ts         # XSS 防护
│   │   ├── errorHandler.ts     # 错误处理
│   │   ├── performance.ts      # 性能监控
│   │   ├── breakpoints.ts      # 断点定义
│   │   └── touchOptimizer.ts   # 触摸优化
│   │
│   ├── views/                  # 页面视图
│   │   ├── HomeView.vue
│   │   ├── LoginView.vue
│   │   ├── RegisterView.vue
│   │   ├── LobbyView.vue
│   │   ├── GameView.vue
│   │   ├── ProfileView.vue
│   │   ├── LeaderboardView.vue
│   │   ├── SettingsView.vue    # 设置页面
│   │   └── NotFoundView.vue
│   │
│   ├── websocket/              # WebSocket 模块
│   │   ├── index.ts
│   │   ├── gomoku.socket.ts
│   │   ├── reconnect.ts        # 重连策略
│   │   ├── messageQueue.ts     # 消息队列
│   │   └── handlers/
│   │
│   ├── App.vue
│   └── main.ts
│
├── .env                        # 环境变量
├── .env.development
├── .env.production
├── index.html
├── package.json
├── tsconfig.json
├── vite.config.ts
├── tailwind.config.js
├── playwright.config.ts        # E2E 测试配置
└── README.md
```

### 2.2 模块职责

| 模块 | 职责 |
|------|------|
| `api/` | 封装所有 HTTP 请求，处理请求/响应拦截 |
| `components/` | 可复用 UI 组件，按功能域分组 |
| `components/mobile/` | 移动端专用组件 |
| `composables/` | 可复用的组合式逻辑 |
| `layouts/` | 页面布局模板 |
| `locales/` | 国际化语言文件 |
| `router/` | 路由配置和导航守卫 |
| `stores/` | 全局状态管理 |
| `types/` | TypeScript 类型定义 |
| `utils/` | 纯函数工具集 |
| `views/` | 页面级视图组件 |
| `websocket/` | WebSocket 连接和消息处理 |

---

## 3. 核心功能模块

### 3.1 认证模块

#### 3.1.1 功能清单

| 功能 | 描述 | 优先级 |
|------|------|--------|
| 用户登录 | 用户名/密码登录 | P0 |
| 用户注册 | 新用户注册 | P0 |
| Token 管理 | Access/Refresh Token 存储、刷新、过期处理 | P0 |
| 自动登录 | 记住登录状态 | P1 |
| 密码重置 | 忘记密码流程 | P2 |
| 多设备登录 | 同账号多设备管理 | P3 |

#### 3.1.2 API 接口

```typescript
// api/auth.api.ts
export const authApi = {
  // 登录
  login: (credentials: LoginRequest) =>
    POST<LoginResponse>('/api/v1/auth/login', credentials),

  // 注册
  register: (data: RegisterRequest) =>
    POST<RegisterResponse>('/api/v1/auth/register', data),

  // 刷新 Token
  refreshToken: (refreshToken: string) =>
    POST<RefreshResponse>('/api/v1/auth/refresh', { refresh_token: refreshToken }),

  // 登出
  logout: () =>
    POST('/api/v1/auth/logout'),

  // 验证 Token
  validateToken: () =>
    GET<ValidateResponse>('/api/v1/auth/validate'),
}
```

#### 3.1.3 状态管理

```typescript
// stores/auth.store.ts
export const useAuthStore = defineStore('auth', {
  state: () => ({
    user: null as User | null,
    accessToken: null as string | null,
    refreshToken: null as string | null,
    isAuthenticated: false,
    loading: false,
    error: null as string | null,
  }),

  actions: {
    async login(credentials: LoginRequest) { ... },
    async register(data: RegisterRequest) { ... },
    async logout() { ... },
    async refreshAccessToken() { ... },
    async validateSession() { ... },
    setTokens(accessToken: string, refreshToken: string) { ... },
    clearAuth() { ... },
  },

  getters: {
    isLoggedIn: (state) => state.isAuthenticated && !!state.accessToken,
    userName: (state) => state.user?.username || 'Guest',
  },
})
```

### 3.2 用户模块

#### 3.2.1 功能清单

| 功能 | 描述 | 优先级 |
|------|------|--------|
| 个人资料 | 查看/编辑用户信息 | P0 |
| 头像上传 | 上传和裁剪头像 | P1 |
| 用户统计 | 游戏统计、胜率等 | P1 |
| 偏好设置 | 主题、语言、通知设置 | P2 |
| 在线状态 | 显示/设置在线状态 | P2 |

#### 3.2.2 API 接口

```typescript
// api/user.api.ts
export const userApi = {
  // 获取用户信息
  getProfile: () =>
    GET<UserProfile>('/api/v1/user/profile'),

  // 更新用户信息
  updateProfile: (data: UpdateProfileRequest) =>
    PUT<UserProfile>('/api/v1/user/profile', data),

  // 获取用户统计
  getStats: (userId?: string) =>
    GET<UserStats>('/api/v1/user/stats', { params: { userId } }),

  // 上传头像
  uploadAvatar: (file: File) => {
    const formData = new FormData()
    formData.append('avatar', file)
    return POST<UploadResponse>('/api/v1/user/avatar', formData)
  },

  // 更新偏好设置
  updatePreferences: (prefs: UserPreferences) =>
    PUT('/api/v1/user/preferences', prefs),
}
```

### 3.3 游戏大厅模块

#### 3.3.1 功能清单

| 功能 | 描述 | 优先级 |
|------|------|--------|
| 房间列表 | 显示所有游戏房间 | P0 |
| 创建房间 | 创建新游戏房间 | P0 |
| 加入房间 | 加入现有房间 | P0 |
| 快速匹配 | 自动匹配对手 | P1 |
| 房间筛选 | 按规则、人数筛选 | P2 |
| 观战模式 | 观看他人对战 | P2 |

#### 3.3.2 API 接口

```typescript
// api/gomoku.api.ts
export const gomokuApi = {
  // 获取房间列表
  getRooms: (filter?: RoomFilter) =>
    GET<RoomListResponse>('/api/v1/gomoku/rooms', { params: filter }),

  // 创建房间
  createRoom: (data: CreateRoomRequest) =>
    POST<Room>('/api/v1/gomoku/rooms', data),

  // 加入房间
  joinRoom: (roomId: string) =>
    POST<JoinRoomResponse>(`/api/v1/gomoku/rooms/${roomId}/join`),

  // 离开房间
  leaveRoom: (roomId: string) =>
    POST(`/api/v1/gomoku/rooms/${roomId}/leave`),

  // 快速匹配
  quickMatch: (preferences: MatchPreferences) =>
    POST<MatchResult>('/api/v1/gomoku/match', preferences),

  // 获取房间详情
  getRoomDetail: (roomId: string) =>
    GET<RoomDetail>(`/api/v1/gomoku/rooms/${roomId}`),
}
```

### 3.4 游戏对战模块

#### 3.4.1 功能清单

| 功能 | 描述 | 优先级 |
|------|------|--------|
| 游戏棋盘 | 15x15 棋盘渲染 | P0 |
| 落子操作 | 点击落子、坐标转换 | P0 |
| 实时同步 | WebSocket 同步棋局 | P0 |
| 回合指示 | 显示当前回合方 | P0 |
| 胜负判定 | 五子连珠检测 | P0 |
| 认输/求和 | 游戏中操作 | P1 |
| 悔棋请求 | 请求撤销上一步 | P1 |
| 历史记录 | 棋谱回放 | P2 |
| 游戏计时 | 回合计时器 | P1 |

#### 3.4.2 WebSocket 消息类型

```typescript
// types/websocket.types.ts

// 客户端 -> 服务器
export type ClientMessage =
  | { type: 'join'; roomId: string; token: string }
  | { type: 'move'; roomId: string; position: Position }
  | { type: 'resign'; roomId: string }
  | { type: 'draw_request'; roomId: string }
  | { type: 'draw_response'; roomId: string; accept: boolean }
  | { type: 'undo_request'; roomId: string }
  | { type: 'undo_response'; roomId: string; accept: boolean }
  | { type: 'chat'; roomId: string; message: string }
  | { type: 'ping' }

// 服务器 -> 客户端
export type ServerMessage =
  | { type: 'joined'; room: Room; color: 'black' | 'white' }
  | { type: 'move'; position: Position; color: 'black' | 'white'; timestamp: number }
  | { type: 'turn'; color: 'black' | 'white'; timeLeft: number }
  | { type: 'game_over'; winner: string | null; reason: GameOverReason }
  | { type: 'player_joined'; user: UserInfo; color: 'black' | 'white' }
  | { type: 'player_left'; userId: string }
  | { type: 'chat'; userId: string; username: string; message: string; timestamp: number }
  | { type: 'draw_requested'; from: string }
  | { type: 'undo_requested'; from: string }
  | { type: 'error'; code: string; message: string }
  | { type: 'pong' }
```

#### 3.4.3 WebSocket 连接管理

```typescript
// composables/useWebSocket.ts
export function useWebSocket(url: string) {
  const socket = ref<WebSocket | null>(null)
  const isConnected = ref(false)
  const lastMessage = ref<ServerMessage | null>(null)
  const error = ref<string | null>(null)

  const connect = (token: string) => {
    const wsUrl = `${url}?token=${token}`
    socket.value = new WebSocket(wsUrl)

    socket.value.onopen = () => {
      isConnected.value = true
      startHeartbeat()
    }

    socket.value.onmessage = (event) => {
      const message: ServerMessage = JSON.parse(event.data)
      lastMessage.value = message
      handleMessage(message)
    }

    socket.value.onerror = (e) => {
      error.value = 'WebSocket connection error'
    }

    socket.value.onclose = () => {
      isConnected.value = false
      stopHeartbeat()
      // 自动重连
      setTimeout(() => connect(token), 3000)
    }
  }

  const send = (message: ClientMessage) => {
    if (socket.value?.readyState === WebSocket.OPEN) {
      socket.value.send(JSON.stringify(message))
    }
  }

  return { connect, disconnect, send, isConnected, lastMessage, error }
}
```

### 3.5 排行榜模块

#### 3.5.1 功能清单

| 功能 | 描述 | 优先级 |
|------|------|--------|
| 排行榜列表 | 显示玩家排名 | P0 |
| 多维度排行 | 积分、胜率、胜场等 | P1 |
| 个人排名 | 显示当前用户排名 | P1 |
| 历史排名 | 排名变化趋势 | P2 |

#### 3.5.2 API 接口

```typescript
// api/leaderboard.api.ts
export const leaderboardApi = {
  // 获取排行榜
  getLeaderboard: (type: LeaderboardType, limit?: number, offset?: number) =>
    GET<LeaderboardResponse>('/api/v1/gamedata/leaderboard', {
      params: { type, limit, offset }
    }),

  // 获取用户排名
  getUserRank: (userId: string) =>
    GET<UserRank>(`/api/v1/gamedata/leaderboard/user/${userId}`),
}
```

### 3.6 成就系统模块

#### 3.6.1 功能清单

| 功能 | 描述 | 优先级 |
|------|------|--------|
| 成就列表 | 显示所有成就及解锁状态 | P1 |
| 成就通知 | 解锁成就时弹出通知 | P1 |
| 成就进度 | 显示成就完成进度 | P2 |
| 成就分享 | 分享成就到社交媒体 | P3 |

---

## 4. API 层设计

### 4.1 Axios 封装

```typescript
// api/request.ts
import axios, { AxiosInstance, AxiosRequestConfig, AxiosResponse, AxiosError } from 'axios'
import { useAuthStore } from '@/stores/auth.store'
import router from '@/router'

const BASE_URL = import.meta.env.VITE_API_BASE_URL || ''

// 创建 Axios 实例
const request: AxiosInstance = axios.create({
  baseURL: BASE_URL,
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
})

// 请求拦截器
request.interceptors.request.use(
  (config) => {
    const authStore = useAuthStore()

    // 添加 Token
    if (authStore.accessToken) {
      config.headers.Authorization = `Bearer ${authStore.accessToken}`
    }

    return config
  },
  (error) => Promise.reject(error)
)

// 响应拦截器
request.interceptors.response.use(
  (response: AxiosResponse) => {
    return response.data
  },
  async (error: AxiosError) => {
    const authStore = useAuthStore()
    const originalRequest = error.config as AxiosRequestConfig & { _retry?: boolean }

    // Token 过期，尝试刷新
    if (error.response?.status === 401 && !originalRequest._retry) {
      originalRequest._retry = true

      try {
        await authStore.refreshAccessToken()
        // 重新发送原请求
        return request(originalRequest)
      } catch (refreshError) {
        // 刷新失败，清除登录状态
        authStore.clearAuth()
        router.push('/login')
        return Promise.reject(refreshError)
      }
    }

    // 其他错误
    const errorMessage = (error.response?.data as any)?.error?.message || error.message
    return Promise.reject(new Error(errorMessage))
  }
)

export default request
```

### 4.2 统一响应格式

```typescript
// types/api.types.ts
export interface ApiResponse<T = any> {
  success: boolean
  data?: T
  error?: {
    code: string
    message: string
  }
  timestamp: number
}

export interface PaginatedResponse<T> extends ApiResponse<T[]> {
  pagination: {
    page: number
    pageSize: number
    total: number
    totalPages: number
  }
}
```

### 4.3 API 模块导出

```typescript
// api/index.ts
export * from './auth.api'
export * from './user.api'
export * from './gomoku.api'
export * from './leaderboard.api'

// 统一请求方法
export const GET = <T>(url: string, config?: AxiosRequestConfig) =>
  request.get<never, T>(url, config)

export const POST = <T>(url: string, data?: any, config?: AxiosRequestConfig) =>
  request.post<never, T>(url, data, config)

export const PUT = <T>(url: string, data?: any, config?: AxiosRequestConfig) =>
  request.put<never, T>(url, data, config)

export const DELETE = <T>(url: string, config?: AxiosRequestConfig) =>
  request.delete<never, T>(url, config)
```

---

## 5. 路由设计

### 5.1 路由配置

```typescript
// router/index.ts
import { createRouter, createWebHistory } from 'vue-router'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/',
      component: () => import('@/layouts/DefaultLayout.vue'),
      children: [
        { path: '', name: 'home', component: () => import('@/views/HomeView.vue') },
        { path: 'lobby', name: 'lobby', component: () => import('@/views/LobbyView.vue'), meta: { auth: true } },
        { path: 'leaderboard', name: 'leaderboard', component: () => import('@/views/LeaderboardView.vue') },
        { path: 'profile', name: 'profile', component: () => import('@/views/ProfileView.vue'), meta: { auth: true } },
      ],
    },
    {
      path: '/game/:roomId',
      name: 'game',
      component: () => import('@/views/GameView.vue'),
      meta: { auth: true },
    },
    {
      path: '/auth',
      component: () => import('@/layouts/AuthLayout.vue'),
      children: [
        { path: 'login', name: 'login', component: () => import('@/views/LoginView.vue') },
        { path: 'register', name: 'register', component: () => import('@/views/RegisterView.vue') },
      ],
    },
    { path: '/:pathMatch(.*)*', name: 'not-found', component: () => import('@/views/NotFoundView.vue') },
  ],
})

export default router
```

### 5.2 导航守卫

```typescript
// router/guards.ts
import { useAuthStore } from '@/stores/auth.store'

export function setupRouterGuards(router: Router) {
  router.beforeEach(async (to, from, next) => {
    const authStore = useAuthStore()

    // 需要认证的页面
    if (to.meta.auth && !authStore.isLoggedIn) {
      return next({ name: 'login', query: { redirect: to.fullPath } })
    }

    // 已登录用户访问登录页
    if ((to.name === 'login' || to.name === 'register') && authStore.isLoggedIn) {
      return next({ name: 'home' })
    }

    next()
  })
}
```

---

## 6. 状态管理

### 6.1 Store 结构

```typescript
// stores/index.ts
import { createPinia } from 'pinia'

export const pinia = createPinia()

// 导出所有 Store
export * from './auth.store'
export * from './user.store'
export * from './game.store'
export * from './notification.store'
```

### 6.2 游戏 Store 示例

```typescript
// stores/game.store.ts
export const useGameStore = defineStore('game', {
  state: () => ({
    // 当前房间
    currentRoom: null as Room | null,

    // 棋盘状态
    board: createEmptyBoard(),
    currentPlayer: 'black' as PieceColor,

    // 游戏状态
    gameStatus: 'waiting' as GameStatus,
    winner: null as string | null,

    // 玩家信息
    blackPlayer: null as Player | null,
    whitePlayer: null as Player | null,
    myColor: null as PieceColor | null,

    // 计时器
    blackTime: 600,
    whiteTime: 600,

    // 历史记录
    moveHistory: [] as Move[],
  }),

  getters: {
    isMyTurn: (state) => state.currentPlayer === state.myColor,
    isGameOver: (state) => state.gameStatus === 'ended',
    opponent: (state) => state.myColor === 'black' ? state.whitePlayer : state.blackPlayer,
  },

  actions: {
    // 落子
    makeMove(position: Position) {
      if (!this.isMyTurn || this.isGameOver) return false
      if (this.board[position.row][position.col] !== null) return false

      this.board[position.row][position.col] = this.myColor
      this.moveHistory.push({ position, color: this.myColor, timestamp: Date.now() })

      return true
    },

    // 处理对手落子
    handleOpponentMove(position: Position, color: PieceColor) {
      this.board[position.row][position.col] = color
      this.currentPlayer = color === 'black' ? 'white' : 'black'
      this.moveHistory.push({ position, color, timestamp: Date.now() })
    },

    // 重置游戏
    resetGame() {
      this.board = createEmptyBoard()
      this.currentPlayer = 'black'
      this.gameStatus = 'playing'
      this.winner = null
      this.moveHistory = []
    },
  },
})
```

---

## 7. 组件设计

### 7.1 五子棋棋盘组件

```vue
<!-- components/gomoku/GomokuBoard.vue -->
<template>
  <div class="gomoku-board" :class="{ 'game-over': isGameOver }">
    <div class="board-container">
      <div
        v-for="(row, rowIndex) in board"
        :key="rowIndex"
        class="board-row"
      >
        <GomokuCell
          v-for="(cell, colIndex) in row"
          :key="colIndex"
          :piece="cell"
          :is-last-move="isLastMove(rowIndex, colIndex)"
          :is-hoverable="isMyTurn && !cell"
          @click="handleCellClick(rowIndex, colIndex)"
        />
      </div>
    </div>

    <!-- 坐标标记 -->
    <div class="coordinates">
      <span v-for="i in 15" :key="`col-${i}`" class="coord col-coord">
        {{ String.fromCharCode(64 + i) }}
      </span>
      <span v-for="i in 15" :key="`row-${i}`" class="coord row-coord">
        {{ 15 - i + 1 }}
      </span>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useGameStore } from '@/stores/game.store'

const gameStore = useGameStore()

const board = computed(() => gameStore.board)
const isMyTurn = computed(() => gameStore.isMyTurn)
const isGameOver = computed(() => gameStore.isGameOver)

const isLastMove = (row: number, col: number) => {
  const lastMove = gameStore.moveHistory[gameStore.moveHistory.length - 1]
  return lastMove?.position.row === row && lastMove?.position.col === col
}

const handleCellClick = (row: number, col: number) => {
  if (!isMyTurn.value || isGameOver.value) return

  // 发送 WebSocket 消息
  // websocket.send({ type: 'move', position: { row, col } })
}
</script>

<style scoped>
.gomoku-board {
  position: relative;
  background: linear-gradient(135deg, #dcb35c 0%, #c9a227 100%);
  border-radius: 8px;
  padding: 20px;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
}

.board-container {
  display: grid;
  gap: 0;
}

.board-row {
  display: flex;
}

.gomoku-board.game-over {
  opacity: 0.8;
  pointer-events: none;
}
</style>
```

### 7.2 用户统计组件

```vue
<!-- components/user/UserStats.vue -->
<template>
  <div class="user-stats">
    <el-card v-if="stats">
      <template #header>
        <div class="card-header">
          <span>游戏统计</span>
          <el-tag :type="stats.online ? 'success' : 'info'">
            {{ stats.online ? '在线' : '离线' }}
          </el-tag>
        </div>
      </template>

      <el-row :gutter="20">
        <el-col :span="6" v-for="stat in statItems" :key="stat.label">
          <div class="stat-item">
            <div class="stat-value">{{ stat.value }}</div>
            <div class="stat-label">{{ stat.label }}</div>
          </div>
        </el-col>
      </el-row>

      <el-divider />

      <div class="win-rate-chart">
        <el-progress
          type="dashboard"
          :percentage="winRate"
          :color="winRateColor"
        >
          <template #default="{ percentage }">
            <span class="percentage-value">{{ percentage }}%</span>
            <span class="percentage-label">胜率</span>
          </template>
        </el-progress>
      </div>
    </el-card>

    <el-skeleton v-else :rows="5" animated />
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

interface Props {
  stats: UserStats | null
}

const props = defineProps<Props>()

const statItems = computed(() => [
  { label: '总场次', value: props.stats?.totalGames || 0 },
  { label: '胜场', value: props.stats?.wins || 0 },
  { label: '负场', value: props.stats?.losses || 0 },
  { label: '平局', value: props.stats?.draws || 0 },
])

const winRate = computed(() => {
  if (!props.stats?.totalGames) return 0
  return Math.round((props.stats.wins / props.stats.totalGames) * 100)
})

const winRateColor = computed(() => {
  if (winRate.value >= 60) return '#67c23a'
  if (winRate.value >= 40) return '#e6a23c'
  return '#f56c6c'
})
</script>
```

---

## 8. 开发阶段规划

### 8.1 阶段一：基础框架（第 1-2 周）

| 任务 | 描述 | 优先级 |
|------|------|--------|
| 项目初始化 | Vite + Vue 3 + TypeScript 项目搭建 | P0 |
| 配置完善 | ESLint、Prettier、Husky、lint-staged | P0 |
| 目录结构 | 创建标准目录结构 | P0 |
| 基础组件 | Header、Footer、Sidebar、Loading | P0 |
| 路由配置 | 路由表、导航守卫 | P0 |
| 状态管理 | Pinia Store 结构 | P0 |
| API 层 | Axios 封装、拦截器 | P0 |
| 样式系统 | TailwindCSS + Element Plus 集成 | P1 |
| 响应式断点 | 断点定义、useBreakpoint composable | P1 |
| 主题系统 | CSS 变量、亮/暗主题切换 | P1 |

### 8.2 阶段二：认证系统（第 3 周）

| 任务 | 描述 | 优先级 |
|------|------|--------|
| 登录页面 | 登录表单、表单验证 | P0 |
| 注册页面 | 注册表单、密码强度检测 | P0 |
| Token 管理 | 存储、刷新、过期处理 | P0 |
| 路由守卫 | 认证状态检查 | P0 |
| 用户信息 | 用户信息获取和展示 | P1 |

### 8.3 阶段三：游戏大厅（第 4 周）

| 任务 | 描述 | 优先级 |
|------|------|--------|
| 大厅页面 | 房间列表展示 | P0 |
| 创建房间 | 房间创建表单 | P0 |
| 加入房间 | 房间加入逻辑 | P0 |
| 快速匹配 | 自动匹配功能 | P1 |
| 房间筛选 | 筛选和搜索 | P2 |

### 8.4 阶段四：游戏对战（第 5-6 周）

| 任务 | 描述 | 优先级 |
|------|------|--------|
| WebSocket | 连接、消息处理 | P0 |
| 棋盘组件 | 15x15 棋盘渲染 | P0 |
| 落子逻辑 | 点击落子、坐标转换 | P0 |
| 实时同步 | 棋局同步、回合指示 | P0 |
| 胜负判定 | 五子连珠检测 | P0 |
| 移动端棋盘 | 自适应尺寸、触摸手势 | P0 |
| 断线重连 | 自动重连、状态恢复 | P1 |
| 游戏计时 | 回合计时器 | P1 |
| 悔棋/求和 | 游戏中操作 | P1 |
| 游戏聊天 | 局内聊天 | P2 |

### 8.5 阶段五：用户中心（第 7 周）

| 任务 | 描述 | 优先级 |
|------|------|--------|
| 个人资料 | 查看/编辑资料 | P0 |
| 用户统计 | 游戏统计数据 | P1 |
| 偏好设置 | 主题、语言设置 | P2 |
| 成就系统 | 成就展示 | P2 |

### 8.6 阶段六：排行榜与优化（第 8 周）

| 任务 | 描述 | 优先级 |
|------|------|--------|
| 排行榜页面 | 排名列表展示 | P1 |
| 性能优化 | 懒加载、缓存、虚拟滚动 | P1 |
| 错误处理 | 全局错误处理、错误上报 | P1 |
| 移动端适配 | 响应式布局、底部导航、安全区域 | P0 |
| 触摸优化 | 手势支持、防误触、快速点击 | P1 |
| 国际化 | Vue I18n 集成、语言切换 | P2 |
| PWA 支持 | 离线功能、Service Worker | P3 |
| 性能监控 | Web Vitals 收集、性能上报 | P2 |

---

## 9. 环境配置

### 9.1 环境变量

```bash
# .env.development
VITE_API_BASE_URL=http://172.26.26.199
VITE_WS_URL=ws://172.26.26.199/ws/gomoku/
VITE_APP_TITLE=Game Platform

# .env.production
VITE_API_BASE_URL=https://api.gameplatform.com
VITE_WS_URL=wss://api.gameplatform.com/ws/gomoku/
VITE_APP_TITLE=Game Platform
```

### 9.2 Vite 配置

```typescript
// vite.config.ts
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { resolve } from 'path'

export default defineConfig({
  plugins: [vue()],
  resolve: {
    alias: {
      '@': resolve(__dirname, 'src'),
    },
  },
  server: {
    port: 3000,
    proxy: {
      '/api': {
        target: 'http://172.26.26.199',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://172.26.26.199',
        ws: true,
      },
    },
  },
  build: {
    rollupOptions: {
      output: {
        manualChunks: {
          'element-plus': ['element-plus'],
          'vue-vendor': ['vue', 'vue-router', 'pinia'],
        },
      },
    },
  },
})
```

---

## 10. 测试策略

### 10.1 单元测试

- **框架**: Vitest
- **覆盖率要求**: 80%+
- **重点**: 组合式函数、工具函数、Store actions

### 10.2 组件测试

- **框架**: Vue Test Utils + Vitest
- **重点**: 交互逻辑、状态变化、事件触发

### 10.3 E2E 测试

- **框架**: Playwright
- **重点**: 关键用户流程
  - 注册 → 登录 → 创建房间 → 对战 → 结束

---

## 11. 部署方案

### 11.1 构建产物

```bash
# 构建
npm run build

# 产物结构
dist/
├── index.html
├── assets/
│   ├── index-[hash].js
│   ├── index-[hash].css
│   └── ...
└── favicon.ico
```

### 11.2 Nginx 配置

```nginx
server {
    listen 80;
    server_name gameplatform.com;

    root /var/www/game-platform-client/dist;
    index index.html;

    # SPA 路由支持
    location / {
        try_files $uri $uri/ /index.html;
    }

    # 静态资源缓存
    location /assets/ {
        expires 1y;
        add_header Cache-Control "public, immutable";
    }

    # API 代理
    location /api/ {
        proxy_pass http://172.26.26.199;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }

    # WebSocket 代理
    location /ws/ {
        proxy_pass http://172.26.26.199;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
    }
}
```

---

## 12. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| WebSocket 连接不稳定 | 游戏中断 | 实现自动重连 + 状态恢复 |
| Token 过期处理不当 | 用户体验差 | 完善刷新机制 + 提前刷新 |
| 移动端兼容问题 | 部分用户无法使用 | 响应式设计 + 兼容性测试 |
| 大量并发请求 | 性能下降 | 请求队列 + 防抖节流 |

---

## 13. 移动端适配方案

### 13.1 响应式设计策略

#### 13.1.1 断点定义

```typescript
// utils/breakpoints.ts
export const BREAKPOINTS = {
  xs: 320,   // 小型手机
  sm: 576,   // 标准手机
  md: 768,   // 平板竖屏
  lg: 992,   // 平板横屏/小型桌面
  xl: 1200,  // 标准桌面
  xxl: 1400, // 大型桌面
} as const

export type Breakpoint = keyof typeof BREAKPOINTS
```

#### 13.1.2 响应式布局适配

```typescript
// composables/useBreakpoint.ts
import { ref, onMounted, onUnmounted } from 'vue'
import { BREAKPOINTS, Breakpoint } from '@/utils/breakpoints'

export function useBreakpoint() {
  const currentBreakpoint = ref<Breakpoint>('lg')
  const isMobile = ref(false)
  const isTablet = ref(false)
  const isDesktop = ref(true)

  const updateBreakpoint = () => {
    const width = window.innerWidth

    if (width < BREAKPOINTS.sm) {
      currentBreakpoint.value = 'xs'
      isMobile.value = true
      isTablet.value = false
      isDesktop.value = false
    } else if (width < BREAKPOINTS.md) {
      currentBreakpoint.value = 'sm'
      isMobile.value = true
      isTablet.value = false
      isDesktop.value = false
    } else if (width < BREAKPOINTS.lg) {
      currentBreakpoint.value = 'md'
      isMobile.value = false
      isTablet.value = true
      isDesktop.value = false
    } else if (width < BREAKPOINTS.xl) {
      currentBreakpoint.value = 'lg'
      isMobile.value = false
      isTablet.value = false
      isDesktop.value = true
    } else {
      currentBreakpoint.value = 'xl'
      isMobile.value = false
      isTablet.value = false
      isDesktop.value = true
    }
  }

  onMounted(() => {
    updateBreakpoint()
    window.addEventListener('resize', updateBreakpoint)
  })

  onUnmounted(() => {
    window.removeEventListener('resize', updateBreakpoint)
  })

  return {
    currentBreakpoint,
    isMobile,
    isTablet,
    isDesktop,
    breakpointValue: BREAKPOINTS,
  }
}
```

### 13.2 移动端棋盘适配

#### 13.2.1 棋盘尺寸自适应

```vue
<!-- components/gomoku/GomokuBoard.vue -->
<template>
  <div class="gomoku-board-container" ref="boardContainer">
    <div
      class="gomoku-board"
      :style="boardStyle"
      :class="{ 'touch-device': isTouchDevice }"
    >
      <!-- 棋盘内容 -->
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useBreakpoint } from '@/composables/useBreakpoint'

const { isMobile, isTablet } = useBreakpoint()
const boardContainer = ref<HTMLElement | null>(null)
const containerSize = ref(0)
const isTouchDevice = ref(false)

// 检测触摸设备
onMounted(() => {
  isTouchDevice.value = 'ontouchstart' in window || navigator.maxTouchPoints > 0
  updateContainerSize()
  window.addEventListener('resize', updateContainerSize)
})

onUnmounted(() => {
  window.removeEventListener('resize', updateContainerSize)
})

const updateContainerSize = () => {
  if (boardContainer.value) {
    const padding = isMobile.value ? 16 : 32
    containerSize.value = Math.min(
      boardContainer.value.clientWidth - padding,
      boardContainer.value.clientHeight - padding,
      isMobile.value ? 360 : isTablet.value ? 480 : 560
    )
  }
}

const boardStyle = computed(() => ({
  width: `${containerSize.value}px`,
  height: `${containerSize.value}px`,
  fontSize: `${containerSize.value / 15}px`,
}))
</script>

<style scoped>
.gomoku-board-container {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
}

.gomoku-board.touch-device {
  /* 触摸设备优化 */
  touch-action: manipulation;
  user-select: none;
  -webkit-tap-highlight-color: transparent;
}

@media (max-width: 576px) {
  .gomoku-board-container {
    padding: 8px;
  }
}
</style>
```

#### 13.2.2 触摸手势支持

```typescript
// composables/useGestures.ts
import { ref, onMounted, onUnmounted } from 'vue'

interface GestureCallbacks {
  onTap?: (position: { x: number; y: number }) => void
  onLongPress?: (position: { x: number; y: number }) => void
  onPinch?: (scale: number) => void
  onPan?: (delta: { x: number; y: number }) => void
}

export function useGestures(
  element: Ref<HTMLElement | null>,
  callbacks: GestureCallbacks
) {
  const isLongPress = ref(false)
  let longPressTimer: number | null = null
  let initialDistance = 0
  let lastTouchTime = 0

  // 触摸开始
  const handleTouchStart = (e: TouchEvent) => {
    if (e.touches.length === 1) {
      // 单指触摸 - 可能是点击或长按
      const touch = e.touches[0]
      isLongPress.value = false

      longPressTimer = window.setTimeout(() => {
        isLongPress.value = true
        callbacks.onLongPress?.({ x: touch.clientX, y: touch.clientY })
      }, 500) // 500ms 长按
    } else if (e.touches.length === 2) {
      // 双指触摸 - 缩放手势
      initialDistance = getDistance(e.touches[0], e.touches[1])
    }
  }

  // 触摸移动
  const handleTouchMove = (e: TouchEvent) => {
    // 取消长按
    if (longPressTimer) {
      clearTimeout(longPressTimer)
      longPressTimer = null
    }

    if (e.touches.length === 2) {
      // 双指缩放
      const currentDistance = getDistance(e.touches[0], e.touches[1])
      const scale = currentDistance / initialDistance
      callbacks.onPinch?.(scale)
    } else if (e.touches.length === 1) {
      // 单指滑动
      const touch = e.touches[0]
      callbacks.onPan?.({ x: touch.clientX, y: touch.clientY })
    }
  }

  // 触摸结束
  const handleTouchEnd = (e: TouchEvent) => {
    if (longPressTimer) {
      clearTimeout(longPressTimer)
      longPressTimer = null
    }

    if (!isLongPress.value && e.changedTouches.length === 1) {
      const touch = e.changedTouches[0]
      callbacks.onTap?.({ x: touch.clientX, y: touch.clientY })
    }
  }

  // 计算两点距离
  const getDistance = (touch1: Touch, touch2: Touch) => {
    const dx = touch1.clientX - touch2.clientX
    const dy = touch1.clientY - touch2.clientY
    return Math.sqrt(dx * dx + dy * dy)
  }

  onMounted(() => {
    if (element.value) {
      element.value.addEventListener('touchstart', handleTouchStart, { passive: true })
      element.value.addEventListener('touchmove', handleTouchMove, { passive: true })
      element.value.addEventListener('touchend', handleTouchEnd, { passive: true })
    }
  })

  onUnmounted(() => {
    if (element.value) {
      element.value.removeEventListener('touchstart', handleTouchStart)
      element.value.removeEventListener('touchmove', handleTouchMove)
      element.value.removeEventListener('touchend', handleTouchEnd)
    }
  })
}
```

### 13.3 移动端布局组件

#### 13.3.1 自适应布局

```vue
<!-- layouts/ResponsiveLayout.vue -->
<template>
  <div class="responsive-layout" :class="layoutClass">
    <!-- 移动端顶部导航 -->
    <header v-if="isMobile" class="mobile-header">
      <button class="menu-btn" @click="showSidebar = !showSidebar">
        <MenuIcon />
      </button>
      <h1 class="app-title">{{ appTitle }}</h1>
      <button class="user-btn" @click="showUserMenu = !showUserMenu">
        <UserIcon />
      </button>
    </header>

    <!-- 移动端侧边栏（抽屉式） -->
    <Transition name="slide">
      <aside v-if="showSidebar && isMobile" class="mobile-sidebar">
        <nav class="mobile-nav">
          <RouterLink
            v-for="item in navItems"
            :key="item.path"
            :to="item.path"
            @click="showSidebar = false"
          >
            <component :is="item.icon" />
            <span>{{ item.label }}</span>
          </RouterLink>
        </nav>
      </aside>
    </Transition>

    <!-- 遮罩层 -->
    <Transition name="fade">
      <div
        v-if="showSidebar && isMobile"
        class="sidebar-overlay"
        @click="showSidebar = false"
      />
    </Transition>

    <!-- 桌面端布局 -->
    <template v-if="!isMobile">
      <AppSidebar />
      <main class="main-content">
        <slot />
      </main>
    </template>

    <!-- 移动端布局 -->
    <template v-else>
      <main class="main-content mobile">
        <slot />
      </main>
      <!-- 移动端底部导航 -->
      <nav class="mobile-bottom-nav">
        <RouterLink
          v-for="item in bottomNavItems"
          :key="item.path"
          :to="item.path"
          class="nav-item"
        >
          <component :is="item.icon" />
          <span>{{ item.label }}</span>
        </RouterLink>
      </nav>
    </template>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useBreakpoint } from '@/composables/useBreakpoint'

const { isMobile } = useBreakpoint()
const showSidebar = ref(false)
const showUserMenu = ref(false)

const layoutClass = computed(() => ({
  'mobile-layout': isMobile.value,
  'desktop-layout': !isMobile.value,
}))

const bottomNavItems = [
  { path: '/', label: '首页', icon: 'HomeIcon' },
  { path: '/lobby', label: '大厅', icon: 'GameIcon' },
  { path: '/leaderboard', label: '排行', icon: 'TrophyIcon' },
  { path: '/profile', label: '我的', icon: 'UserIcon' },
]
</script>

<style scoped>
.responsive-layout {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
}

/* 移动端头部 */
.mobile-header {
  position: sticky;
  top: 0;
  z-index: 100;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  background: var(--bg-primary);
  border-bottom: 1px solid var(--border-color);
}

/* 移动端侧边栏 */
.mobile-sidebar {
  position: fixed;
  top: 0;
  left: 0;
  z-index: 200;
  width: 280px;
  height: 100vh;
  background: var(--bg-primary);
  box-shadow: 2px 0 8px rgba(0, 0, 0, 0.15);
}

.sidebar-overlay {
  position: fixed;
  inset: 0;
  z-index: 150;
  background: rgba(0, 0, 0, 0.5);
}

/* 移动端底部导航 */
.mobile-bottom-nav {
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  z-index: 100;
  display: flex;
  justify-content: space-around;
  padding: 8px 0;
  padding-bottom: max(8px, env(safe-area-inset-bottom));
  background: var(--bg-primary);
  border-top: 1px solid var(--border-color);
}

.nav-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  padding: 8px 16px;
  color: var(--text-secondary);
  text-decoration: none;
  font-size: 12px;
}

.nav-item.router-link-active {
  color: var(--primary-color);
}

/* 动画 */
.slide-enter-active,
.slide-leave-active {
  transition: transform 0.3s ease;
}

.slide-enter-from,
.slide-leave-to {
  transform: translateX(-100%);
}

.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.3s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}

/* 安全区域适配 */
@supports (padding: max(0px)) {
  .mobile-bottom-nav {
    padding-bottom: max(8px, env(safe-area-inset-bottom));
  }

  .main-content.mobile {
    padding-bottom: max(60px, calc(60px + env(safe-area-inset-bottom)));
  }
}
</style>
```

### 13.4 移动端性能优化

#### 13.4.1 触摸事件优化

```typescript
// utils/touchOptimizer.ts

// 防止默认触摸行为导致的延迟
export function optimizeTouchEvents() {
  // 禁用双击缩放
  document.addEventListener('touchstart', (e) => {
    if (e.touches.length > 1) {
      e.preventDefault()
    }
  }, { passive: false })

  // 禁用长按弹出菜单
  document.addEventListener('contextmenu', (e) => {
    if (window.innerWidth < 768) {
      e.preventDefault()
    }
  })
}

// 快速点击（解决 300ms 延迟）
export function useFastClick(element: Ref<HTMLElement | null>, callback: () => void) {
  let touchStartTime = 0
  let touchStartPos = { x: 0, y: 0 }

  const handleTouchStart = (e: TouchEvent) => {
    touchStartTime = Date.now()
    touchStartPos = {
      x: e.touches[0].clientX,
      y: e.touches[0].clientY,
    }
  }

  const handleTouchEnd = (e: TouchEvent) => {
    const touchEndTime = Date.now()
    const touchEndPos = {
      x: e.changedTouches[0].clientX,
      y: e.changedTouches[0].clientY,
    }

    // 检查是否为快速点击（< 200ms）且移动距离小（< 10px）
    const timeDiff = touchEndTime - touchStartTime
    const distance = Math.sqrt(
      Math.pow(touchEndPos.x - touchStartPos.x, 2) +
      Math.pow(touchEndPos.y - touchStartPos.y, 2)
    )

    if (timeDiff < 200 && distance < 10) {
      callback()
    }
  }

  onMounted(() => {
    if (element.value) {
      element.value.addEventListener('touchstart', handleTouchStart, { passive: true })
      element.value.addEventListener('touchend', handleTouchEnd, { passive: true })
    }
  })

  onUnmounted(() => {
    if (element.value) {
      element.value.removeEventListener('touchstart', handleTouchStart)
      element.value.removeEventListener('touchend', handleTouchEnd)
    }
  })
}
```

#### 13.4.2 移动端渲染优化

```typescript
// utils/mobileRender.ts

// 减少重绘和回流
export function optimizeMobileRendering() {
  // 使用 CSS transform 代替 top/left
  // 使用 will-change 提示浏览器优化
  const style = document.createElement('style')
  style.textContent = `
    .gomoku-cell {
      will-change: transform;
      transform: translateZ(0);
    }
    .game-piece {
      will-change: opacity, transform;
      backface-visibility: hidden;
    }
  `
  document.head.appendChild(style)
}

// 检测设备性能并调整渲染质量
export function usePerformanceAdaptation() {
  const isLowEndDevice = ref(false)
  const animationQuality = ref<'high' | 'medium' | 'low'>('high')

  onMounted(() => {
    // 检测设备内存
    const memory = (navigator as any).deviceMemory
    if (memory && memory < 4) {
      isLowEndDevice.value = true
      animationQuality.value = 'low'
    }

    // 检测 CPU 核心数
    const cores = navigator.hardwareConcurrency
    if (cores && cores < 4) {
      animationQuality.value = 'medium'
    }
  })

  return { isLowEndDevice, animationQuality }
}
```

### 13.5 PWA 离线支持

#### 13.5.1 Service Worker 配置

```typescript
// vite.config.ts - PWA 插件配置
import { VitePWA } from 'vite-plugin-pwa'

export default defineConfig({
  plugins: [
    VitePWA({
      registerType: 'autoUpdate',
      includeAssets: ['favicon.ico', 'robots.txt', 'apple-touch-icon.png'],
      manifest: {
        name: '游戏平台',
        short_name: '游戏平台',
        description: '在线五子棋游戏平台',
        theme_color: '#1890ff',
        background_color: '#ffffff',
        display: 'standalone',
        orientation: 'portrait',
        start_url: '/',
        icons: [
          {
            src: 'pwa-192x192.png',
            sizes: '192x192',
            type: 'image/png',
          },
          {
            src: 'pwa-512x512.png',
            sizes: '512x512',
            type: 'image/png',
          },
          {
            src: 'pwa-512x512.png',
            sizes: '512x512',
            type: 'image/png',
            purpose: 'any maskable',
          },
        ],
      },
      workbox: {
        // 运行时缓存策略
        runtimeCaching: [
          {
            urlPattern: /^https:\/\/.*\/api\/v1\//,
            handler: 'NetworkFirst',
            options: {
              cacheName: 'api-cache',
              expiration: {
                maxEntries: 100,
                maxAgeSeconds: 60 * 5, // 5 分钟
              },
            },
          },
          {
            urlPattern: /\.(?:png|jpg|jpeg|svg|gif)$/,
            handler: 'CacheFirst',
            options: {
              cacheName: 'image-cache',
              expiration: {
                maxEntries: 50,
                maxAgeSeconds: 60 * 60 * 24 * 7, // 1 周
              },
            },
          },
        ],
      },
    }),
  ],
})
```

#### 13.5.2 离线状态管理

```typescript
// composables/useOffline.ts
import { ref, onMounted, onUnmounted } from 'vue'

export function useOffline() {
  const isOnline = ref(navigator.onLine)
  const showOfflineBanner = ref(false)

  const handleOnline = () => {
    isOnline.value = true
    showOfflineBanner.value = false
  }

  const handleOffline = () => {
    isOnline.value = false
    showOfflineBanner.value = true
  }

  onMounted(() => {
    window.addEventListener('online', handleOnline)
    window.addEventListener('offline', handleOffline)
  })

  onUnmounted(() => {
    window.removeEventListener('online', handleOnline)
    window.removeEventListener('offline', handleOffline)
  })

  return {
    isOnline,
    showOfflineBanner,
  }
}
```

### 13.6 移动端测试矩阵

| 设备类型 | 分辨率 | 测试重点 |
|----------|--------|----------|
| iPhone SE | 375x667 | 最小屏幕适配 |
| iPhone 14 | 390x844 | 标准手机适配 |
| iPhone 14 Pro Max | 430x932 | 大屏手机适配 |
| iPad Mini | 768x1024 | 小平板适配 |
| iPad Pro | 1024x1366 | 大平板适配 |
| Android (Pixel) | 412x915 | Android 适配 |
| Android (Samsung) | 360x800 | 三星设备适配 |

---

## 14. 国际化 (i18n) 支持

### 14.1 技术方案

使用 **Vue I18n** 实现多语言支持。

```bash
npm install vue-i18n@9
```

### 14.2 目录结构

```
src/
├── locales/
│   ├── index.ts           # i18n 配置
│   ├── zh-CN/             # 简体中文
│   │   ├── common.json
│   │   ├── auth.json
│   │   ├── game.json
│   │   └── user.json
│   ├── en-US/             # 英文
│   │   ├── common.json
│   │   ├── auth.json
│   │   ├── game.json
│   │   └── user.json
│   └── ja-JP/             # 日文（可选）
│       └── ...
```

### 14.3 配置实现

```typescript
// locales/index.ts
import { createI18n } from 'vue-i18n'
import zhCN from './zh-CN'
import enUS from './en-US'

// 获取浏览器语言或存储的语言偏好
function getDefaultLocale(): string {
  const stored = localStorage.getItem('locale')
  if (stored) return stored

  const browserLang = navigator.language
  if (browserLang.startsWith('zh')) return 'zh-CN'
  if (browserLang.startsWith('en')) return 'en-US'

  return 'zh-CN'
}

const i18n = createI18n({
  legacy: false,
  locale: getDefaultLocale(),
  fallbackLocale: 'en-US',
  messages: {
    'zh-CN': zhCN,
    'en-US': enUS,
  },
})

export default i18n

// 切换语言
export function setLocale(locale: string) {
  i18n.global.locale.value = locale as any
  localStorage.setItem('locale', locale)
  document.documentElement.setAttribute('lang', locale)
}
```

### 14.4 语言文件示例

```json
// locales/zh-CN/game.json
{
  "lobby": {
    "title": "游戏大厅",
    "createRoom": "创建房间",
    "joinRoom": "加入房间",
    "quickMatch": "快速匹配",
    "spectators": "观战"
  },
  "board": {
    "blackTurn": "黑方回合",
    "whiteTurn": "白方回合",
    "yourTurn": "轮到你了",
    "waiting": "等待对手...",
    "victory": "胜利！",
    "defeat": "失败"
  },
  "actions": {
    "resign": "认输",
    "requestDraw": "求和",
    "requestUndo": "悔棋",
    "confirm": "确认",
    "cancel": "取消"
  }
}
```

```json
// locales/en-US/game.json
{
  "lobby": {
    "title": "Game Lobby",
    "createRoom": "Create Room",
    "joinRoom": "Join Room",
    "quickMatch": "Quick Match",
    "spectators": "Spectate"
  },
  "board": {
    "blackTurn": "Black's Turn",
    "whiteTurn": "White's Turn",
    "yourTurn": "Your Turn",
    "waiting": "Waiting for opponent...",
    "victory": "Victory!",
    "defeat": "Defeat"
  },
  "actions": {
    "resign": "Resign",
    "requestDraw": "Request Draw",
    "requestUndo": "Request Undo",
    "confirm": "Confirm",
    "cancel": "Cancel"
  }
}
```

### 14.5 组件中使用

```vue
<template>
  <div class="game-header">
    <h2>{{ t('game.lobby.title') }}</h2>
    <button @click="createRoom">{{ t('game.lobby.createRoom') }}</button>
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n'

const { t } = useI18n()
</script>
```

---

## 15. 全局错误处理与监控

### 15.1 错误边界组件

```vue
<!-- components/common/ErrorBoundary.vue -->
<template>
  <slot v-if="!hasError" />
  <div v-else class="error-boundary">
    <div class="error-content">
      <h2>出了点问题</h2>
      <p>{{ errorMessage }}</p>
      <div class="error-actions">
        <button @click="retry">重试</button>
        <button @click="goHome">返回首页</button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onErrorCaptured } from 'vue'
import { useRouter } from 'vue-router'

const router = useRouter()
const hasError = ref(false)
const errorMessage = ref('')

onErrorCaptured((error: Error, instance, info) => {
  hasError.value = true
  errorMessage.value = error.message || '未知错误'

  // 上报错误
  reportError(error, info)

  // 阻止错误继续传播
  return false
})

const retry = () => {
  hasError.value = false
  errorMessage.value = ''
}

const goHome = () => {
  router.push('/')
}
</script>
```

### 15.2 全局错误处理

```typescript
// utils/errorHandler.ts
import { AxiosError } from 'axios'

interface ErrorReport {
  message: string
  stack?: string
  url: string
  timestamp: number
  userAgent: string
  extra?: Record<string, any>
}

// 错误上报函数
async function reportError(error: Error, extra?: Record<string, any>) {
  const report: ErrorReport = {
    message: error.message,
    stack: error.stack,
    url: window.location.href,
    timestamp: Date.now(),
    userAgent: navigator.userAgent,
    extra,
  }

  // 开发环境打印
  if (import.meta.env.DEV) {
    console.error('Error Report:', report)
    return
  }

  // 生产环境上报
  try {
    await fetch('/api/v1/errors/report', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(report),
    })
  } catch (e) {
    console.error('Failed to report error:', e)
  }
}

// 初始化全局错误处理
export function setupErrorHandling(app: App) {
  // Vue 错误
  app.config.errorHandler = (error, instance, info) => {
    reportError(error as Error, { componentInfo: info })
  }

  // 全局未捕获的 Promise 错误
  window.addEventListener('unhandledrejection', (event) => {
    reportError(new Error(event.reason), { type: 'unhandledrejection' })
  })

  // 全局 JavaScript 错误
  window.addEventListener('error', (event) => {
    reportError(new Error(event.message), {
      filename: event.filename,
      lineno: event.lineno,
      colno: event.colno,
    })
  })
}

// API 错误处理
export function handleApiError(error: unknown): string {
  if (error instanceof AxiosError) {
    const status = error.response?.status
    const message = error.response?.data?.error?.message

    switch (status) {
      case 401:
        return '登录已过期，请重新登录'
      case 403:
        return '没有权限执行此操作'
      case 404:
        return '请求的资源不存在'
      case 500:
        return '服务器错误，请稍后重试'
      default:
        return message || '网络请求失败'
    }
  }

  return (error as Error).message || '未知错误'
}
```

### 15.3 性能监控集成

```typescript
// utils/performance.ts

// Web Vitals 指标收集
export function initPerformanceMonitoring() {
  // 首次内容绘制 (FCP)
  observePerformance('paint', (entries) => {
    const fcp = entries.find((e) => e.name === 'first-contentful-paint')
    if (fcp) {
      reportMetric('FCP', fcp.startTime)
    }
  })

  // 最大内容绘制 (LCP)
  observePerformance('largest-contentful-paint', (entries) => {
    const lcp = entries[entries.length - 1]
    reportMetric('LCP', lcp.startTime)
  })

  // 首次输入延迟 (FID)
  observePerformance('first-input', (entries) => {
    const fid = entries[0]
    reportMetric('FID', fid.processingStart - fid.startTime)
  })

  // 累积布局偏移 (CLS)
  let clsValue = 0
  observePerformance('layout-shift', (entries) => {
    for (const entry of entries) {
      if (!entry.hadRecentInput) {
        clsValue += entry.value
      }
    }
    reportMetric('CLS', clsValue)
  })
}

function observePerformance(
  type: string,
  callback: (entries: PerformanceEntry[]) => void
) {
  try {
    const observer = new PerformanceObserver((list) => {
      callback(list.getEntries())
    })
    observer.observe({ type, buffered: true })
  } catch (e) {
    // 浏览器不支持
  }
}

function reportMetric(name: string, value: number) {
  if (import.meta.env.DEV) {
    console.log(`[Performance] ${name}: ${value.toFixed(2)}ms`)
    return
  }

  // 生产环境上报
  navigator.sendBeacon('/api/v1/metrics', JSON.stringify({ name, value }))
}
```

---

## 16. 安全增强措施

### 16.1 XSS 防护

```typescript
// utils/sanitize.ts

/**
 * HTML 实体编码
 */
export function escapeHtml(str: string): string {
  const escapeMap: Record<string, string> = {
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;',
    '/': '&#x2F;',
  }
  return str.replace(/[&<>"'/]/g, (char) => escapeMap[char])
}

/**
 * 安全的 HTML 渲染（仅允许特定标签）
 */
export function sanitizeHtml(html: string): string {
  const allowedTags = ['b', 'i', 'em', 'strong', 'span']
  const temp = document.createElement('div')
  temp.textContent = html
  return temp.innerHTML
}

/**
 * URL 安全检查
 */
export function isSafeUrl(url: string): boolean {
  try {
    const parsed = new URL(url, window.location.origin)
    // 只允许同源链接
    return parsed.origin === window.location.origin
  } catch {
    return false
  }
}
```

### 16.2 CSRF 防护

```typescript
// api/request.ts

// 从 meta 标签获取 CSRF Token
function getCsrfToken(): string | null {
  const meta = document.querySelector('meta[name="csrf-token"]')
  return meta?.getAttribute('content') || null
}

// 请求拦截器添加 CSRF Token
request.interceptors.request.use((config) => {
  const csrfToken = getCsrfToken()
  if (csrfToken && ['POST', 'PUT', 'DELETE'].includes(config.method?.toUpperCase() || '')) {
    config.headers['X-CSRF-Token'] = csrfToken
  }
  return config
})
```

### 16.3 敏感数据处理

```typescript
// utils/crypto.ts

/**
 * 简单的客户端加密（用于非关键数据）
 * 注意：敏感数据应由服务端加密
 */
export function encryptData(data: string, key: string): string {
  let result = ''
  for (let i = 0; i < data.length; i++) {
    result += String.fromCharCode(data.charCodeAt(i) ^ key.charCodeAt(i % key.length))
  }
  return btoa(result)
}

export function decryptData(encrypted: string, key: string): string {
  const data = atob(encrypted)
  let result = ''
  for (let i = 0; i < data.length; i++) {
    result += String.fromCharCode(data.charCodeAt(i) ^ key.charCodeAt(i % key.length))
  }
  return result
}

/**
 * Token 安全存储
 */
export const secureStorage = {
  setToken(key: string, value: string) {
    // 使用 sessionStorage 存储敏感 token（关闭浏览器即清除）
    sessionStorage.setItem(key, value)
  },

  getToken(key: string): string | null {
    return sessionStorage.getItem(key)
  },

  removeToken(key: string) {
    sessionStorage.removeItem(key)
  },

  clear() {
    sessionStorage.clear()
  },
}
```

### 16.4 Content Security Policy

```html
<!-- index.html -->
<meta
  http-equiv="Content-Security-Policy"
  content="
    default-src 'self';
    script-src 'self' 'unsafe-inline' 'unsafe-eval';
    style-src 'self' 'unsafe-inline';
    img-src 'self' data: https:;
    connect-src 'self' ws: wss:;
    font-src 'self' data:;
  "
/>
```

---

## 17. WebSocket 状态恢复机制

### 17.1 断线重连策略

```typescript
// websocket/reconnect.ts

interface ReconnectConfig {
  maxRetries: number
  baseDelay: number
  maxDelay: number
  jitterFactor: number
}

const defaultConfig: ReconnectConfig = {
  maxRetries: 10,
  baseDelay: 1000,    // 1秒
  maxDelay: 30000,    // 30秒
  jitterFactor: 0.3,  // 30% 抖动
}

export function useReconnect(config: ReconnectConfig = defaultConfig) {
  const retryCount = ref(0)
  const isReconnecting = ref(false)
  const reconnectTimer = ref<number | null>(null)

  // 计算重连延迟（指数退避 + 抖动）
  const getDelay = (attempt: number): number => {
    const exponentialDelay = Math.min(
      config.baseDelay * Math.pow(2, attempt),
      config.maxDelay
    )
    const jitter = exponentialDelay * config.jitterFactor * Math.random()
    return exponentialDelay + jitter
  }

  const scheduleReconnect = (callback: () => void): boolean => {
    if (retryCount.value >= config.maxRetries) {
      console.error('Max reconnection attempts reached')
      return false
    }

    isReconnecting.value = true
    const delay = getDelay(retryCount.value)

    console.log(`Reconnecting in ${delay}ms (attempt ${retryCount.value + 1}/${config.maxRetries})`)

    reconnectTimer.value = window.setTimeout(() => {
      retryCount.value++
      callback()
    }, delay)

    return true
  }

  const resetRetryCount = () => {
    retryCount.value = 0
    isReconnecting.value = false
    if (reconnectTimer.value) {
      clearTimeout(reconnectTimer.value)
      reconnectTimer.value = null
    }
  }

  return {
    retryCount,
    isReconnecting,
    scheduleReconnect,
    resetRetryCount,
  }
}
```

### 17.2 消息队列与状态恢复

```typescript
// websocket/messageQueue.ts

interface QueuedMessage {
  id: string
  message: ClientMessage
  timestamp: number
  retryCount: number
}

export function useMessageQueue() {
  const queue = ref<QueuedMessage[]>([])
  const maxQueueSize = 100
  const maxRetries = 3

  // 生成唯一消息 ID
  const generateMessageId = (): string => {
    return `${Date.now()}-${Math.random().toString(36).substr(2, 9)}`
  }

  // 添加消息到队列
  const enqueue = (message: ClientMessage): string => {
    if (queue.value.length >= maxQueueSize) {
      // 移除最旧的消息
      queue.value.shift()
    }

    const id = generateMessageId()
    queue.value.push({
      id,
      message,
      timestamp: Date.now(),
      retryCount: 0,
    })

    return id
  }

  // 确认消息已发送
  const acknowledge = (messageId: string) => {
    const index = queue.value.findIndex((m) => m.id === messageId)
    if (index !== -1) {
      queue.value.splice(index, 1)
    }
  }

  // 获取待重发的消息
  const getPendingMessages = (): QueuedMessage[] => {
    const now = Date.now()
    return queue.value.filter((m) => {
      // 超过 5 秒未确认的消息需要重发
      return now - m.timestamp > 5000 && m.retryCount < maxRetries
    })
  }

  // 标记消息重试
  const markRetry = (messageId: string) => {
    const msg = queue.value.find((m) => m.id === messageId)
    if (msg) {
      msg.retryCount++
      msg.timestamp = Date.now()
    }
  }

  // 清空队列
  const clear = () => {
    queue.value = []
  }

  return {
    queue,
    enqueue,
    acknowledge,
    getPendingMessages,
    markRetry,
    clear,
  }
}
```

### 17.3 游戏状态同步

```typescript
// composables/useGameStateSync.ts

interface SyncState {
  lastMoveIndex: number
  boardChecksum: string
  timestamp: number
}

export function useGameStateSync() {
  const gameStore = useGameStore()
  const lastSyncState = ref<SyncState | null>(null)

  // 计算棋盘校验和
  const calculateChecksum = (board: (PieceColor | null)[][]): string => {
    let hash = ''
    for (const row of board) {
      for (const cell of row) {
        hash += cell === null ? '0' : cell === 'black' ? '1' : '2'
      }
    }
    return btoa(hash)
  }

  // 生成同步状态
  const generateSyncState = (): SyncState => ({
    lastMoveIndex: gameStore.moveHistory.length,
    boardChecksum: calculateChecksum(gameStore.board),
    timestamp: Date.now(),
  })

  // 验证状态一致性
  const verifyState = (serverState: SyncState): boolean => {
    const currentChecksum = calculateChecksum(gameStore.board)
    return currentChecksum === serverState.boardChecksum
  }

  // 断线重连后同步
  const syncAfterReconnect = async () => {
    try {
      // 请求当前游戏状态
      const response = await gomokuApi.getGameState(gameStore.currentRoom?.id || '')

      if (!verifyState(response.state)) {
        // 状态不一致，使用服务端状态
        gameStore.restoreFromServer(response)
        console.warn('Game state was out of sync, restored from server')
      }

      lastSyncState.value = generateSyncState()
    } catch (error) {
      console.error('Failed to sync game state:', error)
    }
  }

  return {
    lastSyncState,
    generateSyncState,
    verifyState,
    syncAfterReconnect,
  }
}
```

---

## 18. 主题系统设计

### 18.1 CSS 变量系统

```css
/* assets/styles/themes.css */

:root {
  /* 亮色主题（默认） */
  --color-primary: #1890ff;
  --color-success: #52c41a;
  --color-warning: #faad14;
  --color-error: #f5222d;

  /* 背景色 */
  --bg-primary: #ffffff;
  --bg-secondary: #f5f5f5;
  --bg-tertiary: #e8e8e8;

  /* 文字色 */
  --text-primary: #262626;
  --text-secondary: #595959;
  --text-tertiary: #8c8c8c;
  --text-inverse: #ffffff;

  /* 边框色 */
  --border-color: #d9d9d9;
  --border-color-light: #f0f0f0;

  /* 阴影 */
  --shadow-sm: 0 1px 2px rgba(0, 0, 0, 0.05);
  --shadow-md: 0 4px 6px rgba(0, 0, 0, 0.1);
  --shadow-lg: 0 10px 15px rgba(0, 0, 0, 0.15);

  /* 棋盘颜色 */
  --board-bg: linear-gradient(135deg, #dcb35c 0%, #c9a227 100%);
  --board-line: #8b6914;
  --piece-black: #1a1a1a;
  --piece-white: #f5f5f5;
}

/* 暗色主题 */
[data-theme='dark'] {
  --color-primary: #177ddc;
  --color-success: #49aa19;
  --color-warning: #d89614;
  --color-error: #d32029;

  --bg-primary: #141414;
  --bg-secondary: #1f1f1f;
  --bg-tertiary: #2a2a2a;

  --text-primary: #ffffff;
  --text-secondary: #a6a6a6;
  --text-tertiary: #595959;
  --text-inverse: #262626;

  --border-color: #434343;
  --border-color-light: #303030;

  --shadow-sm: 0 1px 2px rgba(0, 0, 0, 0.3);
  --shadow-md: 0 4px 6px rgba(0, 0, 0, 0.4);
  --shadow-lg: 0 10px 15px rgba(0, 0, 0, 0.5);

  --board-bg: linear-gradient(135deg, #5c4a1f 0%, #3d3010 100%);
  --board-line: #a68b2a;
  --piece-black: #2a2a2a;
  --piece-white: #e8e8e8;
}
```

### 18.2 主题切换 Composable

```typescript
// composables/useTheme.ts
import { ref, watch } from 'vue'

export type Theme = 'light' | 'dark' | 'system'

export function useTheme() {
  const theme = ref<Theme>(
    (localStorage.getItem('theme') as Theme) || 'system'
  )

  // 应用主题
  const applyTheme = (newTheme: Theme) => {
    let effectiveTheme = newTheme

    if (newTheme === 'system') {
      effectiveTheme = window.matchMedia('(prefers-color-scheme: dark)').matches
        ? 'dark'
        : 'light'
    }

    document.documentElement.setAttribute('data-theme', effectiveTheme)
    localStorage.setItem('theme', newTheme)
  }

  // 初始化
  applyTheme(theme.value)

  // 监听系统主题变化
  window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
    if (theme.value === 'system') {
      applyTheme('system')
    }
  })

  // 监听主题设置变化
  watch(theme, (newTheme) => {
    applyTheme(newTheme)
  })

  const setTheme = (newTheme: Theme) => {
    theme.value = newTheme
  }

  const toggleTheme = () => {
    theme.value = theme.value === 'dark' ? 'light' : 'dark'
  }

  return {
    theme,
    setTheme,
    toggleTheme,
  }
}
```

### 18.3 主题切换组件

```vue
<!-- components/common/ThemeSwitch.vue -->
<template>
  <button
    class="theme-switch"
    @click="toggleTheme"
    :title="theme === 'dark' ? '切换到亮色模式' : '切换到暗色模式'"
  >
    <SunIcon v-if="theme === 'dark'" />
    <MoonIcon v-else />
  </button>
</template>

<script setup lang="ts">
import { useTheme } from '@/composables/useTheme'

const { theme, toggleTheme } = useTheme()
</script>

<style scoped>
.theme-switch {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 40px;
  height: 40px;
  border: none;
  border-radius: 50%;
  background: var(--bg-secondary);
  color: var(--text-primary);
  cursor: pointer;
  transition: all 0.3s ease;
}

.theme-switch:hover {
  background: var(--bg-tertiary);
}
</style>
```

---

## 19. CI/CD 流程

### 19.1 GitHub Actions 配置

```yaml
# .github/workflows/ci.yml
name: CI/CD Pipeline

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main, develop]

env:
  NODE_VERSION: '20'

jobs:
  lint:
    name: Lint
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: pnpm/action-setup@v2
        with:
          version: 8
      - uses: actions/setup-node@v4
        with:
          node-version: ${{ env.NODE_VERSION }}
          cache: 'pnpm'
      - run: pnpm install
      - run: pnpm lint

  type-check:
    name: Type Check
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: pnpm/action-setup@v2
        with:
          version: 8
      - uses: actions/setup-node@v4
        with:
          node-version: ${{ env.NODE_VERSION }}
          cache: 'pnpm'
      - run: pnpm install
      - run: pnpm type-check

  test:
    name: Test
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: pnpm/action-setup@v2
        with:
          version: 8
      - uses: actions/setup-node@v4
        with:
          node-version: ${{ env.NODE_VERSION }}
          cache: 'pnpm'
      - run: pnpm install
      - run: pnpm test:coverage
      - uses: codecov/codecov-action@v3
        with:
          files: ./coverage/lcov.info

  e2e:
    name: E2E Tests
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: pnpm/action-setup@v2
        with:
          version: 8
      - uses: actions/setup-node@v4
        with:
          node-version: ${{ env.NODE_VERSION }}
          cache: 'pnpm'
      - run: pnpm install
      - run: pnpm build
      - run: pnpm e2e
      - uses: actions/upload-artifact@v3
        if: failure()
        with:
          name: playwright-report
          path: playwright-report/

  build:
    name: Build
    runs-on: ubuntu-latest
    needs: [lint, type-check, test]
    steps:
      - uses: actions/checkout@v4
      - uses: pnpm/action-setup@v2
        with:
          version: 8
      - uses: actions/setup-node@v4
        with:
          node-version: ${{ env.NODE_VERSION }}
          cache: 'pnpm'
      - run: pnpm install
      - run: pnpm build
      - uses: actions/upload-artifact@v3
        with:
          name: dist
          path: dist/

  deploy-staging:
    name: Deploy Staging
    runs-on: ubuntu-latest
    needs: [build, e2e]
    if: github.ref == 'refs/heads/develop'
    steps:
      - uses: actions/download-artifact@v3
        with:
          name: dist
          path: dist
      - name: Deploy to staging
        run: |
          # 部署到预发布环境
          echo "Deploying to staging..."

  deploy-production:
    name: Deploy Production
    runs-on: ubuntu-latest
    needs: [build, e2e]
    if: github.ref == 'refs/heads/main'
    steps:
      - uses: actions/download-artifact@v3
        with:
          name: dist
          path: dist
      - name: Deploy to production
        run: |
          # 部署到生产环境
          echo "Deploying to production..."
```

### 19.2 版本管理

```json
// package.json scripts
{
  "scripts": {
    "dev": "vite",
    "build": "vue-tsc && vite build",
    "preview": "vite preview",
    "lint": "eslint . --ext .vue,.js,.jsx,.cjs,.mjs,.ts,.tsx --fix",
    "type-check": "vue-tsc --noEmit",
    "test": "vitest",
    "test:coverage": "vitest run --coverage",
    "e2e": "playwright test",
    "release": "standard-version",
    "release:minor": "standard-version --release-as minor",
    "release:major": "standard-version --release-as major"
  }
}
```

---

## 20. 附录

### 20.1 TypeScript 类型定义

```typescript
// types/game.types.ts
export type PieceColor = 'black' | 'white'
export type GameStatus = 'waiting' | 'playing' | 'ended'
export type GameOverReason = 'win' | 'resign' | 'draw' | 'timeout'

export interface Position {
  row: number
  col: number
}

export interface Move {
  position: Position
  color: PieceColor
  timestamp: number
}

export interface Player {
  id: string
  username: string
  rating: number
  avatar?: string
}

export interface Room {
  id: string
  name: string
  type: 'public' | 'private'
  rules: GameRules
  status: GameStatus
  players: Player[]
  createdAt: number
}

export interface GameRules {
  boardSize: number
  timeLimit: number
  undoAllowed: boolean
  spectatorAllowed: boolean
}
```

### 20.2 组合式函数示例

```typescript
// composables/useNotification.ts
import { ElNotification } from 'element-plus'

export function useNotification() {
  const showSuccess = (message: string, title = '成功') => {
    ElNotification.success({ title, message })
  }

  const showError = (message: string, title = '错误') => {
    ElNotification.error({ title, message })
  }

  const showWarning = (message: string, title = '警告') => {
    ElNotification.warning({ title, message })
  }

  const showInfo = (message: string, title = '提示') => {
    ElNotification.info({ title, message })
  }

  return { showSuccess, showError, showWarning, showInfo }
}
```

### 20.3 移动端检测 Hook

```typescript
// composables/useDevice.ts
import { ref, onMounted } from 'vue'

export function useDevice() {
  const isMobile = ref(false)
  const isTablet = ref(false)
  const isDesktop = ref(true)
  const isTouchDevice = ref(false)
  const isIOS = ref(false)
  const isAndroid = ref(false)
  const screenWidth = ref(0)
  const screenHeight = ref(0)
  const pixelRatio = ref(1)

  const detect = () => {
    const ua = navigator.userAgent

    // 设备类型检测
    isMobile.value = /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(ua) &&
                     window.innerWidth < 768
    isTablet.value = /iPad|Android(?!.*Mobile)/i.test(ua) ||
                     (window.innerWidth >= 768 && window.innerWidth < 1024)
    isDesktop.value = !isMobile.value && !isTablet.value

    // 触摸设备检测
    isTouchDevice.value = 'ontouchstart' in window ||
                          navigator.maxTouchPoints > 0

    // 操作系统检测
    isIOS.value = /iPad|iPhone|iPod/.test(ua)
    isAndroid.value = /Android/.test(ua)

    // 屏幕信息
    screenWidth.value = window.innerWidth
    screenHeight.value = window.innerHeight
    pixelRatio.value = window.devicePixelRatio || 1
  }

  onMounted(() => {
    detect()
    window.addEventListener('resize', detect)
  })

  return {
    isMobile,
    isTablet,
    isDesktop,
    isTouchDevice,
    isIOS,
    isAndroid,
    screenWidth,
    screenHeight,
    pixelRatio,
  }
}
```

### 20.4 安全区域适配

```typescript
// composables/useSafeArea.ts
import { ref, onMounted } from 'vue'

export function useSafeArea() {
  const top = ref(0)
  const bottom = ref(0)
  const left = ref(0)
  const right = ref(0)

  const updateSafeArea = () => {
    const computedStyle = getComputedStyle(document.documentElement)

    top.value = parseInt(computedStyle.getPropertyValue('--safe-area-inset-top') || '0')
    bottom.value = parseInt(computedStyle.getPropertyValue('--safe-area-inset-bottom') || '0')
    left.value = parseInt(computedStyle.getPropertyValue('--safe-area-inset-left') || '0')
    right.value = parseInt(computedStyle.getPropertyValue('--safe-area-inset-right') || '0')
  }

  onMounted(() => {
    // 设置 CSS 环境变量
    document.documentElement.style.setProperty('--safe-area-inset-top', 'env(safe-area-inset-top)')
    document.documentElement.style.setProperty('--safe-area-inset-bottom', 'env(safe-area-inset-bottom)')
    document.documentElement.style.setProperty('--safe-area-inset-left', 'env(safe-area-inset-left)')
    document.documentElement.style.setProperty('--safe-area-inset-right', 'env(safe-area-inset-right)')

    updateSafeArea()
    window.addEventListener('resize', updateSafeArea)
  })

  return { top, bottom, left, right }
}
```

---

## 21. 总结

本开发计划涵盖了一个完整的游戏平台 Web 客户端的开发流程，从项目架构、技术选型、模块设计到具体的开发阶段规划。重点包括：

1. **模块化架构**：清晰的目录结构和模块划分
2. **完善的认证系统**：JWT Token 管理和自动刷新
3. **实时游戏对战**：WebSocket 连接和状态同步
4. **用户体验优化**：响应式设计、错误处理、性能优化
5. **移动端适配**：触摸手势、响应式布局、PWA 支持
6. **国际化支持**：Vue I18n 多语言方案
7. **安全增强**：XSS/CSRF 防护、敏感数据保护
8. **主题系统**：亮色/暗色主题切换
9. **CI/CD 流程**：自动化构建、测试、部署

### 开发周期

预计开发周期为 **8 周**，可根据实际资源情况调整。

### 技术栈总览

| 类别 | 技术选型 |
|------|----------|
| 框架 | Vue 3 + TypeScript |
| 构建 | Vite 5 |
| 状态管理 | Pinia |
| 路由 | Vue Router 4 |
| HTTP | Axios |
| WebSocket | 原生 WebSocket + Socket.io-client |
| UI 组件 | Element Plus + TailwindCSS |
| 国际化 | Vue I18n 9 |
| 测试 | Vitest + Playwright |
| PWA | vite-plugin-pwa |
