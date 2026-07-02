<template>
  <div class="profile-page">
    <!-- 顶部导航栏 -->
    <div class="top-nav-bar">
      <el-button text @click="goBack" class="back-btn">
        <el-icon><ArrowLeft /></el-icon>
        返回首页
      </el-button>
      <div class="nav-spacer"></div>
    </div>

    <!-- 页面头部 -->
    <div class="profile-header">
      <div class="header-content">
        <div class="avatar-section">
          <el-avatar :size="100" :src="profile?.avatar_url" class="user-avatar">
            {{ profile?.nickname?.[0]?.toUpperCase() || userInfo?.username?.[0]?.toUpperCase() }}
          </el-avatar>
          <div class="user-basic-info">
            <h1 class="username">{{ profile?.nickname || userInfo?.username || '用户' }}</h1>
            <el-tag v-if="userInfo" :type="getStatusType(userInfo.status)" effect="dark">
              {{ getStatusText(userInfo.status) }}
            </el-tag>
          </div>
        </div>
        <div class="level-badge" v-if="gameProfile">
          <div class="level-icon">
            <el-icon :size="32"><Trophy /></el-icon>
          </div>
          <div class="level-info">
            <span class="level-value">Lv.{{ gameProfile.level }}</span>
            <span class="level-label">当前等级</span>
          </div>
        </div>
      </div>
    </div>

    <!-- 主内容区 -->
    <div class="profile-container">
      <!-- 左侧栏：快捷统计 -->
      <div class="profile-sidebar">
        <!-- 段位卡片 v2.6.0 -->
        <div class="sidebar-card tier-card" v-if="gameProfile">
          <h3 class="card-title">
            <el-icon><Trophy /></el-icon>
            段位信息
          </h3>
          <div class="tier-display">
            <TierBadge
              v-if="currentTier !== null"
              :tier="currentTier"
              :rating="gameProfile.current_rating"
              size="large"
              show-rating
            />
          </div>
          <div class="tier-progress" v-if="tierProgress && tierProgress.nextTier">
            <div class="progress-header">
              <span class="progress-label">距下一段位</span>
              <span class="progress-value">{{ tierProgress.needed }}分</span>
            </div>
            <el-progress
              :percentage="tierProgress.progress"
              :stroke-width="8"
              :show-text="false"
              color="#fbbf24"
            />
          </div>
          <div class="rating-stats">
            <div class="rating-stat-item">
              <span class="rating-stat-value">{{ gameProfile.current_rating }}</span>
              <span class="rating-stat-label">当前评分</span>
            </div>
            <div class="rating-stat-item">
              <span class="rating-stat-value">{{ gameProfile.peak_rating }}</span>
              <span class="rating-stat-label">最高评分</span>
            </div>
          </div>
        </div>

        <!-- 游戏统计卡片 v2.6.0 -->
        <div class="sidebar-card stats-card" v-if="gameProfile">
          <h3 class="card-title">
            <el-icon><DataLine /></el-icon>
            对局统计
          </h3>
          <div class="game-stats">
            <div class="game-stat-row">
              <div class="game-stat-item win">
                <span class="game-stat-value">{{ gameProfile.wins }}</span>
                <span class="game-stat-label">胜</span>
              </div>
              <div class="game-stat-item lose">
                <span class="game-stat-value">{{ gameProfile.losses }}</span>
                <span class="game-stat-label">负</span>
              </div>
              <div class="game-stat-item draw">
                <span class="game-stat-value">{{ gameProfile.draws }}</span>
                <span class="game-stat-label">平</span>
              </div>
            </div>
            <div class="game-stat-summary">
              <div class="summary-item">
                <span class="summary-label">总场次</span>
                <span class="summary-value">{{ gameProfile.total_games }}</span>
              </div>
              <div class="summary-item">
                <span class="summary-label">胜率</span>
                <span class="summary-value" :class="{ 'high-winrate': winRate >= 50 }">{{ winRate }}%</span>
              </div>
            </div>
            <div class="streak-info" v-if="gameProfile.current_win_streak > 0">
              <span class="streak-icon">🔥</span>
              <span class="streak-text">当前连胜 {{ gameProfile.current_win_streak }} 局</span>
            </div>
            <div class="streak-info best" v-if="gameProfile.best_win_streak > 0">
              <span class="streak-icon">👑</span>
              <span class="streak-text">最高连胜 {{ gameProfile.best_win_streak }} 局</span>
            </div>
          </div>
        </div>

        <!-- 货币卡片 -->
        <div class="sidebar-card currency-card">
          <h3 class="card-title">
            <el-icon><Coin /></el-icon>
            资产
          </h3>
          <div class="currency-list">
            <div class="currency-item">
              <div class="currency-icon gold">
                <el-icon><Coin /></el-icon>
              </div>
              <div class="currency-info">
                <span class="currency-value">{{ coins.toLocaleString() }}</span>
                <span class="currency-label">金币</span>
              </div>
            </div>
            <div class="currency-item">
              <div class="currency-icon gem">
                <el-icon><StarFilled /></el-icon>
              </div>
              <div class="currency-info">
                <span class="currency-value">{{ gems.toLocaleString() }}</span>
                <span class="currency-label">宝石</span>
              </div>
            </div>
            <div class="currency-item">
              <div class="currency-icon token">
                <el-icon><Present /></el-icon>
              </div>
              <div class="currency-info">
                <span class="currency-value">{{ tokens.toLocaleString() }}</span>
                <span class="currency-label">代币</span>
              </div>
            </div>
          </div>
        </div>

        <!-- 成就统计卡片 -->
        <div class="sidebar-card achievement-card">
          <h3 class="card-title">
            <el-icon><Medal /></el-icon>
            成就
          </h3>
          <div class="achievement-stats">
            <div class="stat-item">
              <span class="stat-value">{{ totalAchievements }}</span>
              <span class="stat-label">已解锁</span>
            </div>
            <div class="stat-divider"></div>
            <div class="stat-item">
              <span class="stat-value">{{ totalAchievementPoints }}</span>
              <span class="stat-label">总积分</span>
            </div>
          </div>
          <el-button type="primary" text class="view-all-btn" @click="showAllAchievements = true">
            查看全部成就
            <el-icon><ArrowRight /></el-icon>
          </el-button>
        </div>
      </div>

      <!-- 右侧主内容 -->
      <div class="profile-main">
        <!-- 基础信息卡片 -->
        <div class="content-card">
          <div class="card-header">
            <h2>
              <el-icon><User /></el-icon>
              基础信息
            </h2>
          </div>
          <el-form :model="userForm" label-width="100px" class="profile-form">
            <el-form-item label="用户名">
              <el-input v-model="userForm.username" disabled />
            </el-form-item>
            <el-form-item label="邮箱">
              <el-input v-model="userForm.email" />
            </el-form-item>
            <el-form-item label="手机号">
              <el-input v-model="userForm.phone" placeholder="请输入手机号" />
            </el-form-item>
            <el-form-item>
              <el-button type="primary" @click="handleSaveUserInfo" :loading="loading">
                保存基础信息
              </el-button>
            </el-form-item>
          </el-form>
        </div>

        <!-- 档案信息卡片 -->
        <div class="content-card">
          <div class="card-header">
            <h2>
              <el-icon><Document /></el-icon>
              个人档案
            </h2>
          </div>
          <el-form :model="profileForm" label-width="100px" class="profile-form">
            <el-row :gutter="20">
              <el-col :span="12">
                <el-form-item label="昵称">
                  <el-input v-model="profileForm.nickname" placeholder="请输入昵称" />
                </el-form-item>
              </el-col>
              <el-col :span="12">
                <el-form-item label="真实姓名">
                  <el-input v-model="profileForm.real_name" placeholder="请输入真实姓名" />
                </el-form-item>
              </el-col>
            </el-row>
            <el-form-item label="个人简介">
              <el-input
                v-model="profileForm.bio"
                type="textarea"
                :rows="3"
                placeholder="介绍一下自己..."
                maxlength="500"
                show-word-limit
              />
            </el-form-item>
            <el-row :gutter="20">
              <el-col :span="12">
                <el-form-item label="所在地">
                  <el-input v-model="profileForm.location" placeholder="请输入所在城市" />
                </el-form-item>
              </el-col>
              <el-col :span="12">
                <el-form-item label="个人网站">
                  <el-input v-model="profileForm.website" placeholder="https://example.com" />
                </el-form-item>
              </el-col>
            </el-row>
            <el-row :gutter="20">
              <el-col :span="12">
                <el-form-item label="性别">
                  <el-select v-model="profileForm.gender" placeholder="请选择" style="width: 100%">
                    <el-option label="男" value="male" />
                    <el-option label="女" value="female" />
                    <el-option label="其他" value="other" />
                    <el-option label="保密" value="unknown" />
                  </el-select>
                </el-form-item>
              </el-col>
              <el-col :span="12">
                <el-form-item label="语言">
                  <el-select v-model="profileForm.language" placeholder="请选择" style="width: 100%">
                    <el-option label="简体中文" value="zh-CN" />
                    <el-option label="English" value="en-US" />
                  </el-select>
                </el-form-item>
              </el-col>
            </el-row>
            <el-form-item>
              <el-button type="primary" @click="handleSaveProfile" :loading="profileLoading">
                保存档案
              </el-button>
            </el-form-item>
          </el-form>
        </div>

        <!-- 偏好设置卡片 -->
        <div class="content-card">
          <div class="card-header">
            <h2>
              <el-icon><Setting /></el-icon>
              偏好设置
            </h2>
          </div>
          <div v-if="preferencesLoading" class="loading-state">
            <el-skeleton :rows="5" animated />
          </div>
          <el-form v-else :model="preferencesForm" label-width="120px" class="profile-form">
            <div class="settings-section">
              <h4 class="section-title">通知设置</h4>
              <el-row :gutter="20">
                <el-col :span="8">
                  <el-form-item label="邮件通知">
                    <el-switch v-model="preferencesForm.email_notifications" />
                  </el-form-item>
                </el-col>
                <el-col :span="8">
                  <el-form-item label="推送通知">
                    <el-switch v-model="preferencesForm.push_notifications" />
                  </el-form-item>
                </el-col>
                <el-col :span="8">
                  <el-form-item label="短信通知">
                    <el-switch v-model="preferencesForm.sms_notifications" />
                  </el-form-item>
                </el-col>
              </el-row>
            </div>

            <div class="settings-section">
              <h4 class="section-title">界面设置</h4>
              <el-row :gutter="20">
                <el-col :span="12">
                  <el-form-item label="主题">
                    <el-radio-group v-model="preferencesForm.theme">
                      <el-radio value="light">浅色</el-radio>
                      <el-radio value="dark">深色</el-radio>
                    </el-radio-group>
                  </el-form-item>
                </el-col>
                <el-col :span="12">
                  <el-form-item label="界面语言">
                    <el-select v-model="preferencesForm.language" style="width: 100%">
                      <el-option label="简体中文" value="zh-CN" />
                      <el-option label="English" value="en-US" />
                    </el-select>
                  </el-form-item>
                </el-col>
              </el-row>
            </div>

            <div class="settings-section">
              <h4 class="section-title">音效设置</h4>
              <el-row :gutter="20">
                <el-col :span="8">
                  <el-form-item label="音效">
                    <el-switch v-model="preferencesForm.sound_effects" />
                  </el-form-item>
                </el-col>
                <el-col :span="8">
                  <el-form-item label="背景音乐">
                    <el-switch v-model="preferencesForm.music" />
                  </el-form-item>
                </el-col>
                <el-col :span="8">
                  <el-form-item label="音量">
                    <el-slider v-model="preferencesForm.volume" :min="0" :max="100" />
                  </el-form-item>
                </el-col>
              </el-row>
            </div>

            <div class="settings-section">
              <h4 class="section-title">登录设置</h4>
              <el-row :gutter="20">
                <el-col :span="8">
                  <el-form-item label="自动登录">
                    <el-switch v-model="preferencesForm.auto_login" />
                  </el-form-item>
                </el-col>
                <el-col :span="8">
                  <el-form-item label="记住我">
                    <el-switch v-model="preferencesForm.remember_me" />
                  </el-form-item>
                </el-col>
                <el-col :span="8">
                  <el-form-item label="会话超时">
                    <el-select v-model="preferencesForm.session_timeout" style="width: 100%">
                      <el-option label="5分钟" :value="300" />
                      <el-option label="15分钟" :value="900" />
                      <el-option label="30分钟" :value="1800" />
                      <el-option label="1小时" :value="3600" />
                    </el-select>
                  </el-form-item>
                </el-col>
              </el-row>
            </div>

            <el-form-item>
              <el-button type="primary" @click="handleSavePreferences" :loading="preferencesSaving">
                保存偏好设置
              </el-button>
            </el-form-item>
          </el-form>
        </div>

        <!-- 游戏数据卡片 -->
        <div class="content-card" v-if="gameProfile">
          <div class="card-header">
            <h2>
              <el-icon><DataLine /></el-icon>
              游戏数据
            </h2>
          </div>
          <div class="game-stats-grid">
            <div class="stat-card">
              <div class="stat-icon level">
                <el-icon><TrendCharts /></el-icon>
              </div>
              <div class="stat-content">
                <span class="stat-value">{{ gameProfile.level }}</span>
                <span class="stat-label">等级</span>
              </div>
            </div>
            <div class="stat-card">
              <div class="stat-icon exp">
                <el-icon><Star /></el-icon>
              </div>
              <div class="stat-content">
                <span class="stat-value">{{ gameProfile.experience_points?.toLocaleString() }}</span>
                <span class="stat-label">经验值</span>
              </div>
            </div>
            <div class="stat-card">
              <div class="stat-icon name">
                <el-icon><User /></el-icon>
              </div>
              <div class="stat-content">
                <span class="stat-value">{{ gameProfile.display_name }}</span>
                <span class="stat-label">游戏昵称</span>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <el-dialog v-model="showAllAchievements" title="我的成就" width="90%" top="5vh">
      <AchievementList />
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import {
  Trophy,
  User,
  Document,
  Setting,
  Coin,
  StarFilled,
  Present,
  Medal,
  ArrowRight,
  ArrowLeft,
  DataLine,
  TrendCharts,
  Star,
} from '@element-plus/icons-vue'
import { useUserStore, useAuthStore, useAchievementStore, useGameDataStore } from '@/stores'
import type { UserInfo, UserProfile, UserPreferences } from '@/types/user.types'
import AchievementList from '@/components/user/AchievementList.vue'
import TierBadge from '@/components/common/TierBadge.vue'
import { getTierByRating, getRatingToNextTier } from '@/types/tier.types'

const router = useRouter()
const userStore = useUserStore()
const authStore = useAuthStore()
const achievementStore = useAchievementStore()
const gameDataStore = useGameDataStore()

// State
const userInfo = ref<UserInfo | null>(null)
const profile = ref<UserProfile | null>(null)
const preferences = ref<UserPreferences | null>(null)
const loading = ref(false)
const profileLoading = ref(false)
const preferencesLoading = ref(false)
const preferencesSaving = ref(false)
const gameDataLoading = ref(false)
const showAllAchievements = ref(false)

// Forms
const userForm = reactive({
  username: '',
  email: '',
  phone: '',
})

const profileForm = reactive({
  nickname: '',
  real_name: '',
  bio: '',
  location: '',
  website: '',
  gender: 'unknown' as 'male' | 'female' | 'other' | 'unknown',
  language: 'zh-CN',
})

const preferencesForm = reactive({
  email_notifications: true,
  push_notifications: true,
  sms_notifications: false,
  theme: 'light' as 'light' | 'dark',
  language: 'zh-CN',
  sound_effects: true,
  music: true,
  volume: 80,
  auto_login: false,
  remember_me: true,
  session_timeout: 3600,
})

// Computed
const gameProfile = computed(() => gameDataStore.gameProfile)
const coins = computed(() => gameDataStore.coins)
const gems = computed(() => gameDataStore.gems)
const tokens = computed(() => gameDataStore.tokens)

const totalAchievements = computed(() => achievementStore.totalAchievements)
const totalAchievementPoints = computed(() => achievementStore.totalPoints)

// v2.6.0: 段位和统计数据
const currentTier = computed(() => {
  if (!gameProfile.value?.current_rating) return null
  return getTierByRating(gameProfile.value.current_rating)
})

const tierProgress = computed(() => {
  if (!gameProfile.value?.current_rating) return null
  return getRatingToNextTier(gameProfile.value.current_rating)
})

const winRate = computed(() => {
  if (!gameProfile.value) return 0

  // 优先使用服务端返回的 win_rate（0.0-1.0）
  if (gameProfile.value.win_rate != null) {
    return Math.round(gameProfile.value.win_rate * 100)
  }

  // 回退：从 wins/losses/draws 计算
  const { wins, losses, draws } = gameProfile.value
  const total = wins + losses + draws
  if (total === 0) return 0
  return Math.round((wins / total) * 100)
})

// Helpers
const goBack = () => {
  router.push('/')
}

const getStatusType = (status: string) => {
  const types: Record<string, string> = {
    active: 'success',
    inactive: 'info',
    suspended: 'warning',
    banned: 'danger',
    deleted: 'danger',
  }
  return types[status] || 'info'
}

const getStatusText = (status: string) => {
  const texts: Record<string, string> = {
    active: '正常',
    inactive: '未激活',
    suspended: '已暂停',
    banned: '已封禁',
    deleted: '已删除',
  }
  return texts[status] || status
}

// Handlers
const handleSaveUserInfo = async () => {
  if (!authStore.user?.id) {
    ElMessage.error('用户未登录')
    return
  }

  loading.value = true
  try {
    await userStore.updateUserInfo(authStore.user.id, {
      email: userForm.email,
      phone: userForm.phone,
    })
    ElMessage.success('基础信息保存成功')
  } catch {
    ElMessage.error('保存失败')
  } finally {
    loading.value = false
  }
}

const handleSaveProfile = async () => {
  if (!authStore.user?.id) {
    ElMessage.error('用户未登录')
    return
  }

  profileLoading.value = true
  try {
    await userStore.updateProfile(authStore.user.id, {
      nickname: profileForm.nickname,
      real_name: profileForm.real_name,
      bio: profileForm.bio,
      location: profileForm.location,
      website: profileForm.website,
      gender: profileForm.gender,
      language: profileForm.language,
    })
    ElMessage.success('档案保存成功')
  } catch {
    ElMessage.error('保存失败')
  } finally {
    profileLoading.value = false
  }
}

const handleSavePreferences = async () => {
  if (!authStore.user?.id) {
    ElMessage.error('用户未登录')
    return
  }

  preferencesSaving.value = true
  try {
    await userStore.updatePreferences(authStore.user.id, {
      email_notifications: preferencesForm.email_notifications,
      push_notifications: preferencesForm.push_notifications,
      sms_notifications: preferencesForm.sms_notifications,
      theme: preferencesForm.theme,
      language: preferencesForm.language,
      sound_effects: preferencesForm.sound_effects,
      music: preferencesForm.music,
      volume: preferencesForm.volume,
      auto_login: preferencesForm.auto_login,
      remember_me: preferencesForm.remember_me,
      session_timeout: preferencesForm.session_timeout,
    })
    ElMessage.success('偏好设置保存成功')
  } catch {
    ElMessage.error('保存失败')
  } finally {
    preferencesSaving.value = false
  }
}

// Lifecycle
onMounted(async () => {
  if (!authStore.user?.id) {
    ElMessage.error('用户未登录')
    return
  }

  const userId = authStore.user.id

  // 并行加载数据
  const loadData = async () => {
    // 加载用户基础信息
    try {
      userInfo.value = await userStore.getUserInfo(userId)
      if (userInfo.value) {
        userForm.username = userInfo.value.username
        userForm.email = userInfo.value.email
        userForm.phone = userInfo.value.phone
      }
    } catch {
      // 忽略错误
    }

    // 加载用户档案
    try {
      profile.value = await userStore.getProfile(userId)
      if (profile.value) {
        profileForm.nickname = profile.value.nickname || ''
        profileForm.real_name = profile.value.real_name || ''
        profileForm.bio = profile.value.bio || ''
        profileForm.location = profile.value.location || ''
        profileForm.website = profile.value.website || ''
        profileForm.gender = profile.value.gender || 'unknown'
        profileForm.language = profile.value.language || 'zh-CN'
      }
    } catch {
      // 忽略错误
    }

    // 加载偏好设置
    preferencesLoading.value = true
    try {
      preferences.value = await userStore.getPreferences(userId)
      if (preferences.value) {
        preferencesForm.email_notifications = preferences.value.email_notifications
        preferencesForm.push_notifications = preferences.value.push_notifications
        preferencesForm.sms_notifications = preferences.value.sms_notifications
        preferencesForm.theme = preferences.value.theme
        preferencesForm.language = preferences.value.language
        preferencesForm.sound_effects = preferences.value.sound_effects
        preferencesForm.music = preferences.value.music
        preferencesForm.volume = preferences.value.volume
        preferencesForm.auto_login = preferences.value.auto_login
        preferencesForm.remember_me = preferences.value.remember_me
        preferencesForm.session_timeout = preferences.value.session_timeout
      }
    } catch {
      // 忽略错误
    } finally {
      preferencesLoading.value = false
    }

    // 加载游戏数据（如果档案不存在会自动创建）
    // loadAllGameData 会自动同步成就数据到 achievementStore
    gameDataLoading.value = true
    try {
      const displayName = profileForm.nickname || authStore.user?.username || undefined
      await gameDataStore.loadAllGameData(userId, displayName)
    } catch {
      // 忽略错误
    } finally {
      gameDataLoading.value = false
    }
  }

  await loadData()
})
</script>

<style scoped>
.profile-page {
  min-height: 100vh;
  background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
  padding: 1.5rem;
}

/* 顶部导航栏 */
.top-nav-bar {
  max-width: 1200px;
  margin: 0 auto 1rem;
  display: flex;
  align-items: center;
  padding: 0.5rem 1rem;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 12px;
  border: 1px solid rgba(255, 255, 255, 0.1);
}

.back-btn {
  color: rgba(255, 255, 255, 0.8);
  font-size: 0.9rem;
}

.back-btn:hover {
  color: white;
}

.nav-spacer {
  flex: 1;
}

/* 页面头部 */
.profile-header {
  max-width: 1200px;
  margin: 0 auto 2rem;
  padding: 2rem;
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 20px;
  border: 1px solid rgba(255, 255, 255, 0.1);
}

.header-content {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.avatar-section {
  display: flex;
  align-items: center;
  gap: 1.5rem;
}

.user-avatar {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  font-size: 2.5rem;
  border: 4px solid rgba(255, 255, 255, 0.2);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.3);
}

.user-basic-info {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.username {
  font-size: 1.75rem;
  font-weight: 700;
  color: white;
  margin: 0;
}

.level-badge {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 1rem 1.5rem;
  background: linear-gradient(135deg, rgba(245, 158, 11, 0.2) 0%, rgba(251, 191, 36, 0.2) 100%);
  border-radius: 16px;
  border: 1px solid rgba(245, 158, 11, 0.3);
}

.level-icon {
  width: 56px;
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #f59e0b 0%, #fbbf24 100%);
  border-radius: 14px;
  color: white;
}

.level-info {
  display: flex;
  flex-direction: column;
}

.level-value {
  font-size: 1.5rem;
  font-weight: 700;
  color: #fbbf24;
}

.level-label {
  font-size: 0.875rem;
  color: rgba(255, 255, 255, 0.6);
}

/* 主内容区 */
.profile-container {
  max-width: 1200px;
  margin: 0 auto;
  display: grid;
  grid-template-columns: 280px 1fr;
  gap: 2rem;
}

/* 左侧栏 */
.profile-sidebar {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.sidebar-card {
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(10px);
  border-radius: 16px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  padding: 1.5rem;
}

.card-title {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 1rem;
  font-weight: 600;
  color: white;
  margin: 0 0 1rem;
}

/* 货币卡片 */
.currency-list {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.currency-item {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 0.75rem;
  background: rgba(255, 255, 255, 0.05);
  border-radius: 12px;
}

.currency-icon {
  width: 40px;
  height: 40px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 10px;
  color: white;
}

.currency-icon.gold {
  background: linear-gradient(135deg, #f59e0b 0%, #fbbf24 100%);
}

.currency-icon.gem {
  background: linear-gradient(135deg, #ec4899 0%, #f472b6 100%);
}

.currency-icon.token {
  background: linear-gradient(135deg, #8b5cf6 0%, #a78bfa 100%);
}

.currency-info {
  display: flex;
  flex-direction: column;
}

.currency-value {
  font-size: 1.125rem;
  font-weight: 600;
  color: white;
}

.currency-label {
  font-size: 0.75rem;
  color: rgba(255, 255, 255, 0.5);
}

/* 成就卡片 */
.achievement-stats {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 1.5rem;
  padding: 1rem 0;
}

.stat-item {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.stat-item .stat-value {
  font-size: 1.5rem;
  font-weight: 700;
  color: #fbbf24;
}

.stat-item .stat-label {
  font-size: 0.75rem;
  color: rgba(255, 255, 255, 0.5);
}

.stat-divider {
  width: 1px;
  height: 40px;
  background: rgba(255, 255, 255, 0.1);
}

.view-all-btn {
  width: 100%;
  margin-top: 1rem;
  color: rgba(255, 255, 255, 0.8);
}

.view-all-btn:hover {
  color: white;
}

/* 段位卡片 v2.6.0 */
.tier-card {
  text-align: center;
}

.tier-display {
  padding: 1rem 0;
  display: flex;
  justify-content: center;
}

.tier-progress {
  padding: 0.75rem 0;
  border-top: 1px solid rgba(255, 255, 255, 0.1);
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
  margin-bottom: 0.75rem;
}

.progress-header {
  display: flex;
  justify-content: space-between;
  margin-bottom: 0.5rem;
}

.progress-label {
  font-size: 0.75rem;
  color: rgba(255, 255, 255, 0.6);
}

.progress-value {
  font-size: 0.75rem;
  color: #fbbf24;
  font-weight: 500;
}

.rating-stats {
  display: flex;
  justify-content: space-around;
  padding-top: 0.5rem;
}

.rating-stat-item {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.rating-stat-value {
  font-size: 1.25rem;
  font-weight: 700;
  color: white;
}

.rating-stat-label {
  font-size: 0.7rem;
  color: rgba(255, 255, 255, 0.5);
}

/* 游戏统计卡片 v2.6.0 */
.stats-card {
  text-align: center;
}

.game-stats {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.game-stat-row {
  display: flex;
  justify-content: space-around;
  padding: 0.5rem 0;
}

.game-stat-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 0.5rem 1rem;
}

.game-stat-item .game-stat-value {
  font-size: 1.5rem;
  font-weight: 700;
}

.game-stat-item .game-stat-label {
  font-size: 0.75rem;
  color: rgba(255, 255, 255, 0.5);
}

.game-stat-item.win .game-stat-value {
  color: #10b981;
}

.game-stat-item.lose .game-stat-value {
  color: #ef4444;
}

.game-stat-item.draw .game-stat-value {
  color: #6b7280;
}

.game-stat-summary {
  display: flex;
  justify-content: space-around;
  padding: 0.75rem 0;
  border-top: 1px solid rgba(255, 255, 255, 0.1);
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}

.summary-item {
  display: flex;
  flex-direction: column;
  align-items: center;
}

.summary-label {
  font-size: 0.7rem;
  color: rgba(255, 255, 255, 0.5);
}

.summary-value {
  font-size: 1rem;
  font-weight: 600;
  color: white;
}

.summary-value.high-winrate {
  color: #10b981;
}

.streak-info {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.5rem;
  padding: 0.5rem;
  background: linear-gradient(135deg, rgba(239, 68, 68, 0.1) 0%, rgba(249, 115, 22, 0.1) 100%);
  border-radius: 8px;
}

.streak-info.best {
  background: linear-gradient(135deg, rgba(245, 158, 11, 0.1) 0%, rgba(251, 191, 36, 0.1) 100%);
}

.streak-icon {
  font-size: 1rem;
}

.streak-text {
  font-size: 0.8rem;
  color: rgba(255, 255, 255, 0.8);
}

/* 右侧主内容 */
.profile-main {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.content-card {
  background: rgba(255, 255, 255, 0.95);
  backdrop-filter: blur(20px);
  border-radius: 20px;
  padding: 1.5rem;
  box-shadow: 0 4px 24px rgba(0, 0, 0, 0.1);
}

.content-card .card-header {
  margin-bottom: 1.5rem;
  padding-bottom: 1rem;
  border-bottom: 1px solid #e5e7eb;
}

.content-card .card-header h2 {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 1.25rem;
  font-weight: 600;
  color: #1a1a2e;
  margin: 0;
}

.profile-form {
  max-width: 100%;
}

.settings-section {
  margin-bottom: 1.5rem;
  padding-bottom: 1.5rem;
  border-bottom: 1px solid #f3f4f6;
}

.settings-section:last-of-type {
  border-bottom: none;
  margin-bottom: 1rem;
}

.section-title {
  font-size: 0.875rem;
  font-weight: 500;
  color: #6b7280;
  margin: 0 0 1rem;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

/* 游戏数据统计 */
.game-stats-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 1rem;
}

.stat-card {
  display: flex;
  align-items: center;
  gap: 1rem;
  padding: 1.25rem;
  background: linear-gradient(135deg, #f8fafc 0%, #f1f5f9 100%);
  border-radius: 16px;
  border: 1px solid #e2e8f0;
}

.stat-card .stat-icon {
  width: 48px;
  height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 12px;
  color: white;
}

.stat-card .stat-icon.level {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
}

.stat-card .stat-icon.exp {
  background: linear-gradient(135deg, #10b981 0%, #34d399 100%);
}

.stat-card .stat-icon.name {
  background: linear-gradient(135deg, #f59e0b 0%, #fbbf24 100%);
}

.stat-card .stat-content {
  display: flex;
  flex-direction: column;
}

.stat-card .stat-value {
  font-size: 1.25rem;
  font-weight: 700;
  color: #1a1a2e;
}

.stat-card .stat-label {
  font-size: 0.75rem;
  color: #6b7280;
}

.loading-state {
  padding: 1rem;
}

/* 响应式 */
@media (max-width: 1024px) {
  .profile-container {
    grid-template-columns: 1fr;
  }

  .profile-sidebar {
    flex-direction: row;
    flex-wrap: wrap;
  }

  .sidebar-card {
    flex: 1;
    min-width: 250px;
  }
}

@media (max-width: 768px) {
  .header-content {
    flex-direction: column;
    gap: 1.5rem;
    text-align: center;
  }

  .avatar-section {
    flex-direction: column;
  }

  .username {
    font-size: 1.5rem;
  }

  .profile-sidebar {
    flex-direction: column;
  }

  .sidebar-card {
    min-width: 100%;
  }

  .game-stats-grid {
    grid-template-columns: 1fr;
  }
}
</style>
