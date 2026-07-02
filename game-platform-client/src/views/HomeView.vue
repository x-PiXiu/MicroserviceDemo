<template>
  <div class="home-page">
    <!-- 背景动画 -->
    <div class="background-animation">
      <div class="grid-pattern"></div>
      <div class="floating-particles">
        <div v-for="i in 20" :key="i" class="particle" :style="getParticleStyle(i)"></div>
      </div>
    </div>

    <!-- 主内容 -->
    <div class="home-content">
      <!-- 头部欢迎区 -->
      <div class="welcome-section">
        <div class="user-greeting">
          <el-avatar :size="64" class="user-avatar">
            {{ user?.username?.charAt(0)?.toUpperCase() || 'G' }}
          </el-avatar>
          <div class="greeting-text">
            <h1>{{ getGreeting() }}，<span class="username">{{ user?.username || '游客' }}</span></h1>
            <p class="subtitle">选择你喜欢的游戏，开始精彩对局</p>
          </div>
        </div>

        <!-- 平台统计 -->
        <div class="platform-stats">
          <div class="stat-card">
            <div class="stat-value">{{ animatedStats.onlinePlayers }}</div>
            <div class="stat-label">在线玩家</div>
          </div>
          <div class="stat-card">
            <div class="stat-value">{{ animatedStats.activeRooms }}</div>
            <div class="stat-label">进行中对局</div>
          </div>
          <div class="stat-card">
            <div class="stat-value">{{ animatedStats.todayMatches }}</div>
            <div class="stat-label">今日对局</div>
          </div>
        </div>
      </div>

      <!-- 游戏选择区 -->
      <div class="games-section">
        <div class="section-header">
          <h2>
            <el-icon><Grid /></el-icon>
            选择游戏
          </h2>
          <div class="filter-tabs">
            <el-radio-group v-model="currentFilter" size="small">
              <el-radio-button value="all">全部</el-radio-button>
              <el-radio-button value="board">棋类</el-radio-button>
              <el-radio-button value="featured">推荐</el-radio-button>
            </el-radio-group>
          </div>
        </div>

        <!-- 游戏卡片网格 -->
        <div class="games-grid" v-loading="loading">
          <TransitionGroup name="game-card">
            <div
              v-for="game in filteredGames"
              :key="game.serviceId"
              class="game-card"
              :class="{ featured: game.featured, offline: game.status !== 'online' }"
              @click="handleSelectGame(game)"
            >
              <!-- 游戏图标区 -->
              <div class="game-icon-wrapper" :style="{ background: getGameGradient(game.gameType) }">
                <span class="game-icon">{{ getGameIcon(game.gameType) }}</span>
                <div v-if="game.featured" class="featured-badge">
                  <el-icon><Star /></el-icon>
                  推荐
                </div>
              </div>

              <!-- 游戏信息 -->
              <div class="game-info">
                <div class="game-header">
                  <h3 class="game-name">{{ game.displayName }}</h3>
                  <el-tag
                    :type="game.status === 'online' ? 'success' : 'danger'"
                    size="small"
                    effect="dark"
                  >
                    {{ game.status === 'online' ? '在线' : '离线' }}
                  </el-tag>
                </div>

                <p class="game-description">{{ game.description }}</p>

                <!-- 游戏统计 -->
                <div class="game-stats">
                  <div class="stat">
                    <el-icon><User /></el-icon>
                    <span>{{ game.onlinePlayers }} 在线</span>
                  </div>
                  <div class="stat">
                    <el-icon><House /></el-icon>
                    <span>{{ game.activeRooms }} 房间</span>
                  </div>
                </div>

                <!-- 游戏标签 -->
                <div class="game-tags">
                  <el-tag
                    v-for="tag in game.tags.slice(0, 3)"
                    :key="tag"
                    size="small"
                    type="info"
                    effect="plain"
                  >
                    {{ tag }}
                  </el-tag>
                </div>
              </div>

              <!-- 进入按钮 -->
              <div class="game-action">
                <el-button
                  type="primary"
                  :disabled="game.status !== 'online'"
                  class="enter-btn"
                >
                  <el-icon><Right /></el-icon>
                  进入游戏
                </el-button>
              </div>
            </div>
          </TransitionGroup>
        </div>

        <!-- 空状态 -->
        <el-empty
          v-if="!loading && filteredGames.length === 0"
          description="暂无可用游戏"
        />
      </div>

      <!-- 快捷入口 -->
      <div class="quick-actions">
        <div class="action-card" @click="router.push('/profile')">
          <div class="action-icon" style="background: linear-gradient(135deg, #667eea, #764ba2)">
            <el-icon><User /></el-icon>
          </div>
          <div class="action-text">
            <h4>个人中心</h4>
            <p>查看战绩、成就</p>
          </div>
        </div>
        <div class="action-card" @click="router.push('/leaderboard')">
          <div class="action-icon" style="background: linear-gradient(135deg, #f59e0b, #d97706)">
            <el-icon><TrophyBase /></el-icon>
          </div>
          <div class="action-text">
            <h4>排行榜</h4>
            <p>全服高手榜</p>
          </div>
        </div>
        <div class="action-card" @click="router.push('/settings')">
          <div class="action-icon" style="background: linear-gradient(135deg, #10b981, #059669)">
            <el-icon><Setting /></el-icon>
          </div>
          <div class="action-text">
            <h4>设置</h4>
            <p>个性化配置</p>
          </div>
        </div>
      </div>
    </div>

    <!-- 最近游玩（可选功能） -->
    <div class="recent-section" v-if="recentGames.length > 0">
      <h3>
        <el-icon><Clock /></el-icon>
        最近游玩
      </h3>
      <div class="recent-games">
        <div
          v-for="game in recentGames"
          :key="game.serviceId"
          class="recent-game-item"
          @click="handleSelectGame(game)"
        >
          <span class="recent-icon">{{ getGameIcon(game.gameType) }}</span>
          <span class="recent-name">{{ game.displayName }}</span>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, reactive } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore, useUserStore } from '@/stores'
import { getGameServicesForDisplay } from '@/api/service.api'
import {
  Grid,
  Star,
  User,
  House,
  Right,
  TrophyBase,
  Setting,
  Clock,
} from '@element-plus/icons-vue'
import type { GameService } from '@/types'
import { GameTypeConfigs, getGameTypeInfo } from '@/types/service.types'

const router = useRouter()
const authStore = useAuthStore()
const userStore = useUserStore()
const user = computed(() => authStore.user)

// 状态
const loading = ref(false)
const currentFilter = ref('all')
const services = ref<GameService[]>([])
const recentGames = ref<GameService[]>([])

// 动画统计数字
const animatedStats = reactive({
  onlinePlayers: 0,
  activeRooms: 0,
  todayMatches: 0,
})

const targetStats = reactive({
  onlinePlayers: 0,
  activeRooms: 0,
  todayMatches: 0,
})

// 过滤后的游戏列表
const filteredGames = computed(() => {
  let games = services.value

  if (currentFilter.value === 'board') {
    games = games.filter(g => g.gameType === 'gomoku' || g.gameType === 'chess' || g.gameType === 'reversi')
  } else if (currentFilter.value === 'featured') {
    games = games.filter(g => g.featured)
  }

  // 排序：在线的在前，推荐的优先
  return games.sort((a, b) => {
    if (a.status !== b.status) {
      return a.status === 'online' ? -1 : 1
    }
    if (a.featured !== b.featured) {
      return a.featured ? -1 : 1
    }
    return (b.sortWeight || 0) - (a.sortWeight || 0)
  })
})

// 获取问候语
const getGreeting = () => {
  const hour = new Date().getHours()
  if (hour < 6) return '夜深了'
  if (hour < 12) return '早上好'
  if (hour < 18) return '下午好'
  return '晚上好'
}

// 获取游戏图标
const getGameIcon = (gameType: string) => {
  const info = getGameTypeInfo(gameType)
  return info?.icon || '🎮'
}

// 获取游戏渐变色
const getGameGradient = (gameType: string) => {
  const info = getGameTypeInfo(gameType)
  return info?.gradient || 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)'
}

// 生成粒子样式
const getParticleStyle = (_index: number) => {
  const size = Math.random() * 4 + 2
  return {
    width: `${size}px`,
    height: `${size}px`,
    left: `${Math.random() * 100}%`,
    top: `${Math.random() * 100}%`,
    animationDelay: `${Math.random() * 20}s`,
    animationDuration: `${15 + Math.random() * 20}s`,
  }
}

// 数字动画
const animateNumber = (target: number, current: { value: number }, duration = 2000) => {
  const start = current.value
  const diff = target - start
  const steps = 60
  const stepValue = diff / steps
  const stepDuration = duration / steps
  let step = 0

  const timer = setInterval(() => {
    step++
    current.value = Math.round(start + stepValue * step)
    if (step >= steps) {
      current.value = target
      clearInterval(timer)
    }
  }, stepDuration)
}

// 选择游戏 - v2.0.0: 直接跳转到游戏大厅，游戏服务器信息在 LobbyView 中获取
const handleSelectGame = async (game: GameService) => {
  if (game.status !== 'online') {
    return
  }

  // 保存到最近游玩
  const existing = recentGames.value.findIndex(g => g.serviceId === game.serviceId)
  if (existing > -1) {
    recentGames.value.splice(existing, 1)
  }
  recentGames.value.unshift(game)
  recentGames.value = recentGames.value.slice(0, 4)

  // v2.0.0: 直接跳转到游戏大厅，游戏服务器信息和游戏数据在 LobbyView 中通过服务发现 API 获取
  router.push(`/game/${game.gameType}/lobby`)
}

// 加载服务列表
const loadServices = async () => {
  loading.value = true
  try {
    // 从服务注册中心获取游戏服务列表
    const response = await getGameServicesForDisplay({ healthy_only: true })
    services.value = response.services || []

    // 更新统计目标值：累加所有服务的 metadata 字段
    if (services.value.length > 0) {
      const totalPlayers = services.value.reduce((sum, s) => sum + s.onlinePlayers, 0)
      const totalRooms = services.value.reduce((sum, s) => sum + s.activeRooms, 0)
      const totalMatches = services.value.reduce((sum, s) => sum + s.todayMatches, 0)
      targetStats.onlinePlayers = totalPlayers
      targetStats.activeRooms = totalRooms
      targetStats.todayMatches = totalMatches
    }
  } catch {
    // 使用模拟数据
    services.value = getMockServices()
  } finally {
    loading.value = false
  }

  // 启动统计数字动画
  setTimeout(() => {
    console.log('[HomeView] Starting animations with target values:', {
      onlinePlayers: targetStats.onlinePlayers,
      activeRooms: targetStats.activeRooms,
      todayMatches: targetStats.todayMatches
    })
    console.log('[HomeView] Current animated stats before animation:', {
      onlinePlayers: animatedStats.onlinePlayers,
      activeRooms: animatedStats.activeRooms,
      todayMatches: animatedStats.todayMatches
    })
    animateNumber(targetStats.onlinePlayers, { value: animatedStats.onlinePlayers })
    animateNumber(targetStats.activeRooms, { value: animatedStats.activeRooms })
    animateNumber(targetStats.todayMatches, { value: animatedStats.todayMatches })
  }, 300)
}

// 模拟服务数据（API 调用失败时的 fallback）
const getMockServices = (): GameService[] => {
  const configs = Object.values(GameTypeConfigs)

  return configs.map((config, index) => ({
    serviceId: `svc_${config.id}_001`,
    name: config.name,
    displayName: config.displayName,
    description: config.description,
    icon: config.icon,
    version: '2.0.0',
    httpPort: 8084 + index,
    websocketPort: 8085 + index,
    host: '',
    status: index < 2 ? 'online' : (Math.random() > 0.3 ? 'online' : 'offline'),
    onlinePlayers: Math.floor(Math.random() * 200) + 50,
    activeRooms: Math.floor(Math.random() * 30) + 10,
    todayMatches: Math.floor(Math.random() * 500) + 100,
    gameType: config.id,
    tags: config.tags,
    featured: index === 0,
    sortWeight: 100 - index * 10,
    registeredAt: Date.now() - 86400000 * index,
    lastHeartbeat: Date.now(),
    healthScore: 0.9 + Math.random() * 0.1,
  }))
}

// 加载用户数据（登录后首次进入时）
// 按照业务流程文档 v2.0.0，登录后只加载用户基础信息
// 游戏数据在选择具体游戏时再加载（LobbyView.vue）
const loadUserData = async () => {
  const userId = authStore.user?.id
  if (!userId) return

  try {
    // 阶段二：只加载用户基础信息（User Service）
    // 游戏数据（Game Data Service）在选择游戏时加载
    await Promise.all([
      // User Service 数据：用户档案
      userStore.getProfile(userId).catch(e => {
        console.warn('[HomeView] Failed to load user profile:', e)
      }),
      // User Service 数据：用户偏好设置
      userStore.getPreferences(userId).catch(e => {
        console.warn('[HomeView] Failed to load user preferences:', e)
      }),
    ])

    console.log('[HomeView] User basic info loaded successfully')
  } catch (error) {
    console.error('[HomeView] Failed to load user data:', error)
  }
}

onMounted(() => {
  loadServices()
  loadUserData()
})
</script>

<style scoped>
.home-page {
  min-height: 100vh;
  position: relative;
  overflow: hidden;
  background: linear-gradient(135deg, #0f0c29 0%, #302b63 50%, #24243e 100%);
}

/* 背景动画 */
.background-animation {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  pointer-events: none;
  overflow: hidden;
}

.grid-pattern {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-image:
    linear-gradient(rgba(255, 255, 255, 0.03) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255, 255, 255, 0.03) 1px, transparent 1px);
  background-size: 50px 50px;
}

.floating-particles {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
}

.particle {
  position: absolute;
  background: rgba(102, 126, 234, 0.6);
  border-radius: 50%;
  animation: float-particle 20s infinite ease-in-out;
}

@keyframes float-particle {
  0%, 100% {
    transform: translateY(0) translateX(0);
    opacity: 0;
  }
  10% {
    opacity: 1;
  }
  90% {
    opacity: 1;
  }
  100% {
    transform: translateY(-100vh) translateX(50px);
    opacity: 0;
  }
}

/* 主内容 */
.home-content {
  position: relative;
  z-index: 1;
  max-width: 1400px;
  margin: 0 auto;
  padding: 2rem;
}

/* 欢迎区 */
.welcome-section {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 3rem;
  flex-wrap: wrap;
  gap: 2rem;
}

.user-greeting {
  display: flex;
  align-items: center;
  gap: 1.5rem;
}

.user-avatar {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  font-size: 1.5rem;
  font-weight: 700;
  color: white;
  border: 3px solid rgba(255, 255, 255, 0.2);
}

.greeting-text h1 {
  font-size: 1.75rem;
  font-weight: 700;
  color: white;
  margin: 0 0 0.25rem;
}

.greeting-text .username {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.greeting-text .subtitle {
  color: rgba(255, 255, 255, 0.6);
  margin: 0;
  font-size: 0.95rem;
}

.platform-stats {
  display: flex;
  gap: 1.5rem;
}

.stat-card {
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  padding: 1.25rem 1.75rem;
  text-align: center;
  border: 1px solid rgba(255, 255, 255, 0.1);
  min-width: 120px;
}

.stat-value {
  font-size: 1.75rem;
  font-weight: 700;
  color: white;
  font-family: 'Courier New', monospace;
}

.stat-label {
  font-size: 0.75rem;
  color: rgba(255, 255, 255, 0.6);
  margin-top: 0.25rem;
}

/* 游戏选择区 */
.games-section {
  margin-bottom: 3rem;
}

.section-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
}

.section-header h2 {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  font-size: 1.5rem;
  font-weight: 600;
  color: white;
  margin: 0;
}

.filter-tabs :deep(.el-radio-button__inner) {
  background: rgba(255, 255, 255, 0.1);
  border-color: rgba(255, 255, 255, 0.2);
  color: rgba(255, 255, 255, 0.8);
}

.filter-tabs :deep(.el-radio-button__original-radio:checked + .el-radio-button__inner) {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  border-color: #667eea;
}

/* 游戏卡片网格 */
.games-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(340px, 1fr));
  gap: 1.5rem;
}

.game-card {
  background: rgba(255, 255, 255, 0.95);
  border-radius: 20px;
  overflow: hidden;
  cursor: pointer;
  transition: all 0.3s ease;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.1);
}

.game-card:hover {
  transform: translateY(-8px);
  box-shadow: 0 12px 40px rgba(102, 126, 234, 0.3);
}

.game-card.featured {
  border: 2px solid #f59e0b;
}

.game-card.offline {
  opacity: 0.6;
  cursor: not-allowed;
}

.game-card.offline:hover {
  transform: none;
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.1);
}

.game-icon-wrapper {
  position: relative;
  height: 140px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.game-icon {
  font-size: 4rem;
  filter: drop-shadow(0 4px 8px rgba(0, 0, 0, 0.3));
}

.featured-badge {
  position: absolute;
  top: 12px;
  right: 12px;
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 4px 10px;
  background: linear-gradient(135deg, #f59e0b 0%, #d97706 100%);
  border-radius: 20px;
  font-size: 0.75rem;
  font-weight: 600;
  color: white;
}

.game-info {
  padding: 1.25rem;
}

.game-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.5rem;
}

.game-name {
  font-size: 1.25rem;
  font-weight: 600;
  color: #1a1a2e;
  margin: 0;
}

.game-description {
  color: #6b7280;
  font-size: 0.875rem;
  line-height: 1.5;
  margin-bottom: 0.75rem;
}

.game-stats {
  display: flex;
  gap: 1.5rem;
  margin-bottom: 0.75rem;
}

.game-stats .stat {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 0.8rem;
  color: #6b7280;
}

.game-stats .stat .el-icon {
  color: #667eea;
}

.game-tags {
  display: flex;
  gap: 0.5rem;
  flex-wrap: wrap;
}

.game-action {
  padding: 0 1.25rem 1.25rem;
}

.enter-btn {
  width: 100%;
  height: 44px;
  font-size: 0.95rem;
  font-weight: 600;
  border-radius: 12px;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  border: none;
}

.enter-btn:hover {
  background: linear-gradient(135deg, #5a6fd6 0%, #6a4190 100%);
}

/* 快捷入口 */
.quick-actions {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 1rem;
  margin-bottom: 2rem;
}

.action-card {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 1.25rem;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  cursor: pointer;
  transition: all 0.3s ease;
}

.action-card:hover {
  background: rgba(255, 255, 255, 0.12);
  transform: translateY(-2px);
}

.action-icon {
  width: 48px;
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 12px;
  font-size: 1.25rem;
  color: white;
}

.action-text h4 {
  color: white;
  font-size: 1rem;
  font-weight: 600;
  margin: 0 0 0.25rem;
}

.action-text p {
  color: rgba(255, 255, 255, 0.6);
  font-size: 0.8rem;
  margin: 0;
}

/* 最近游玩 */
.recent-section {
  background: rgba(255, 255, 255, 0.05);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  padding: 1.5rem;
  margin: 0 2rem 2rem;
}

.recent-section h3 {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  color: white;
  font-size: 1rem;
  margin: 0 0 1rem;
}

.recent-games {
  display: flex;
  gap: 1rem;
}

.recent-game-item {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  padding: 0.75rem 1rem;
  background: rgba(255, 255, 255, 0.1);
  border-radius: 10px;
  cursor: pointer;
  transition: all 0.2s;
}

.recent-game-item:hover {
  background: rgba(255, 255, 255, 0.15);
}

.recent-icon {
  font-size: 1.5rem;
}

.recent-name {
  color: white;
  font-size: 0.9rem;
}

/* 卡片动画 */
.game-card-enter-active,
.game-card-leave-active {
  transition: all 0.4s ease;
}

.game-card-enter-from {
  opacity: 0;
  transform: translateY(30px);
}

.game-card-leave-to {
  opacity: 0;
  transform: scale(0.9);
}

/* 响应式 */
@media (max-width: 1024px) {
  .games-grid {
    grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
  }

  .quick-actions {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 768px) {
  .home-content {
    padding: 1rem;
  }

  .welcome-section {
    flex-direction: column;
    align-items: flex-start;
  }

  .platform-stats {
    width: 100%;
    justify-content: space-between;
  }

  .stat-card {
    flex: 1;
    min-width: auto;
    padding: 1rem;
  }

  .stat-value {
    font-size: 1.25rem;
  }

  .section-header {
    flex-direction: column;
    align-items: flex-start;
    gap: 1rem;
  }

  .games-grid {
    grid-template-columns: 1fr;
  }
}
</style>
