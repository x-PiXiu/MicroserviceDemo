<template>
  <header class="app-header">
    <div class="header-container">
      <div class="logo">
        <router-link to="/">
          <h1>游戏平台</h1>
        </router-link>
      </div>
      <nav class="nav">
        <router-link to="/">{{ t('nav.home') }}</router-link>
        <router-link v-if="isAuthenticated" to="/lobby">{{ t('nav.lobby') }}</router-link>
        <router-link to="/leaderboard">{{ t('nav.leaderboard') }}</router-link>
      </nav>

      <!-- 货币展示（仅登录后显示） -->
      <div v-if="isAuthenticated && showCurrency" class="header-currency">
        <CurrencyDisplay mode="inline" size="small" :types="['gold', 'gem']" />
      </div>

      <div class="header-actions">
        <el-button v-if="isAuthenticated" link @click="goToProfile">
          {{ userNameValue }}
        </el-button>
        <template v-if="isAuthenticated">
          <el-button @click="handleLogout">{{ t('auth.logout') }}</el-button>
        </template>
        <template v-else>
          <el-button @click="goToLogin">{{ t('auth.login') }}</el-button>
          <el-button type="primary" @click="goToRegister">{{ t('auth.register') }}</el-button>
        </template>
      </div>
    </div>
  </header>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import { useAuth } from '@/composables'
import { useGameDataStore } from '@/stores'
import { logger } from '@/utils/logger'
import CurrencyDisplay from '@/components/user/CurrencyDisplay.vue'

const { t } = useI18n()
const router = useRouter()
const { isAuthenticated, userName, logout } = useAuth()
const gameDataStore = useGameDataStore()

const userNameValue = computed(() => userName || 'Guest')

// 是否显示货币（登录后且已加载游戏数据）
const showCurrency = computed(() => {
  return isAuthenticated && gameDataStore.initialized && gameDataStore.currencies.length > 0
})

const goToLogin = () => {
  console.log('[AppHeader] goToLogin 被调用')
  logger.component.info('点击登录按钮，跳转到登录页')
  router.push('/login')
}

const goToRegister = () => {
  console.log('[AppHeader] goToRegister 被调用')
  logger.component.info('点击注册按钮，跳转到注册页')
  router.push('/register')
}

const goToProfile = () => {
  router.push('/profile')
}

const handleLogout = async () => {
  await logout()
}
</script>

<style scoped>
.app-header {
  position: sticky;
  top: 0;
  z-index: 1000;
  background: var(--el-bg-color);
  border-bottom: 1px solid var(--el-border-color);
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.1);
}

.header-container {
  max-width: 1200px;
  margin: 0 auto;
  padding: 0 1rem;
  height: 60px;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.logo h1 {
  margin: 0;
  font-size: 1.5rem;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
}

.logo a {
  text-decoration: none;
}

.nav {
  display: flex;
  gap: 2rem;
}

.nav a {
  text-decoration: none;
  color: var(--el-text-color-primary);
  font-weight: 500;
  transition: color 0.3s;
  position: relative;
}

.nav a:hover,
.nav a.router-link-active {
  color: var(--el-color-primary);
}

.nav a.router-link-active::after {
  content: '';
  position: absolute;
  bottom: -22px;
  left: 0;
  right: 0;
  height: 2px;
  background: var(--el-color-primary);
}

.header-actions {
  display: flex;
  gap: 0.5rem;
  align-items: center;
}

.header-currency {
  margin-right: 1rem;
  padding-right: 1rem;
  border-right: 1px solid var(--el-border-color-lighter);
}

@media (max-width: 768px) {
  .nav {
    display: none;
  }

  .header-currency {
    display: none;
  }
}
</style>
