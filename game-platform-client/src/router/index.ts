import { createRouter, createWebHistory } from 'vue-router'
import type { RouteRecordRaw } from 'vue-router'
import { useAuthStore } from '@/stores'

const routes: RouteRecordRaw[] = [
  // 认证相关页面（无布局）
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/views/LoginView.vue'),
    meta: { guest: true },
  },
  {
    path: '/register',
    name: 'Register',
    component: () => import('@/views/RegisterView.vue'),
    meta: { guest: true },
  },
  {
    path: '/forgot-password',
    name: 'ForgotPassword',
    component: () => import('@/views/ForgotPasswordView.vue'),
    meta: { guest: true },
  },

  // 主应用（带默认布局）
  {
    path: '/',
    component: () => import('@/layouts/DefaultLayout.vue'),
    children: [
      // 首页 - 游戏选择大厅
      {
        path: '',
        name: 'Home',
        component: () => import('@/views/HomeView.vue'),
        meta: { title: '游戏大厅' },
      },
      // 用户资料
      {
        path: 'profile',
        name: 'Profile',
        component: () => import('@/views/ProfileView.vue'),
        meta: { auth: true, title: '个人中心' },
      },
      // 排行榜
      {
        path: 'leaderboard',
        name: 'Leaderboard',
        component: () => import('@/views/LeaderboardView.vue'),
        meta: { title: '排行榜' },
      },
      // 设置
      {
        path: 'settings',
        name: 'Settings',
        component: () => import('@/views/SettingsView.vue'),
        meta: { auth: true, title: '设置' },
      },
    ],
  },

  // 游戏模块（动态路由）
  {
    path: '/game/:gameType',
    component: () => import('@/layouts/DefaultLayout.vue'),
    meta: { auth: true },
    children: [
      // 游戏大厅 - 房间列表
      {
        path: 'lobby',
        name: 'GameLobby',
        component: () => import('@/views/LobbyView.vue'),
        meta: { auth: true, title: '游戏大厅' },
      },
      // 游戏房间 - 对局页面
      {
        path: 'room/:roomId',
        name: 'GameRoom',
        component: () => import('@/views/GameView.vue'),
        meta: { auth: true, title: '游戏房间' },
      },
    ],
  },

  // 兼容旧路由（重定向到新路由）
  {
    path: '/lobby',
    redirect: '/game/gomoku/lobby',
  },
  {
    path: '/game/:roomId',
    redirect: (to) => `/game/gomoku/room/${to.params.roomId}`,
  },

  // 404 页面
  {
    path: '/:pathMatch(.*)*',
    name: 'NotFound',
    component: () => import('@/layouts/DefaultLayout.vue'),
    children: [
      {
        path: '',
        component: () => import('@/views/NotFoundView.vue'),
      },
    ],
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
  scrollBehavior(_to, _from, savedPosition) {
    if (savedPosition) {
      return savedPosition
    }
    return { top: 0 }
  },
})

// 路由守卫
router.beforeEach(async (to, _from, next) => {
  const authStore = useAuthStore()

  // 确保从 storage 加载用户信息
  authStore.loadUserFromStorage()

  const isAuthenticated = authStore.isAuthenticated

  console.log('[Router Guard] 导航到:', to.path)
  console.log('[Router Guard] isAuthenticated:', isAuthenticated)

  // 需要认证但未登录
  if (to.meta.auth && !isAuthenticated) {
    console.log('[Router Guard] 需要认证但未登录，重定向到登录页')
    next({ name: 'Login', query: { redirect: to.fullPath } })
    return
  }

  // 已登录但访问登录/注册页面
  if (to.meta.guest && isAuthenticated) {
    console.log('[Router Guard] 已登录状态访问guest页面，重定向到首页')
    next({ name: 'Home' })
    return
  }

  // 设置页面标题
  if (to.meta.title) {
    document.title = `${to.meta.title} - 游戏平台`
  }

  console.log('[Router Guard] 允许导航')
  next()
})

export default router
