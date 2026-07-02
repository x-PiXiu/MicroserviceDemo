<template>
  <div class="settings-page">
    <!-- 顶部导航栏 -->
    <div class="top-nav-bar">
      <el-button text @click="goBack" class="back-btn">
        <el-icon><ArrowLeft /></el-icon>
        返回首页
      </el-button>
      <div class="nav-spacer"></div>
    </div>

    <!-- 设置内容 -->
    <div class="settings-container">
      <el-card class="settings-card">
        <template #header>
          <div class="card-header">
            <el-icon><Setting /></el-icon>
            <h2>{{ t('nav.settings') }}</h2>
          </div>
        </template>
        <el-form label-width="120px">
          <el-form-item label="主题">
            <el-radio-group v-model="preferences.theme" @change="handleThemeChange">
              <el-radio-button value="light">浅色</el-radio-button>
              <el-radio-button value="dark">深色</el-radio-button>
            </el-radio-group>
          </el-form-item>
          <el-form-item label="语言">
            <el-select v-model="preferences.language">
              <el-option label="简体中文" value="zh-CN" />
              <el-option label="English" value="en-US" />
            </el-select>
          </el-form-item>
          <el-form-item label="音效">
            <el-switch v-model="preferences.sound_effects" />
          </el-form-item>
          <el-form-item label="背景音乐">
            <el-switch v-model="preferences.music" />
          </el-form-item>
          <el-form-item label="音量">
            <el-slider v-model="preferences.volume" :min="0" :max="100" />
          </el-form-item>
        </el-form>
      </el-card>
    </div>
  </div>
</template>

<script setup lang="ts">
import { reactive } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { ArrowLeft, Setting } from '@element-plus/icons-vue'
import { useTheme } from '@/composables'

const router = useRouter()
const { t } = useI18n()
const { setTheme } = useTheme()

const preferences = reactive({
  theme: 'light' as 'light' | 'dark',
  language: 'zh-CN',
  sound_effects: true,
  music: true,
  volume: 80,
})

const goBack = () => {
  router.push('/')
}

const handleThemeChange = (value: 'light' | 'dark') => {
  setTheme(value)
}
</script>

<style scoped>
.settings-page {
  min-height: 100vh;
  background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
  padding: 1.5rem;
}

/* 顶部导航栏 */
.top-nav-bar {
  max-width: 800px;
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

/* 设置容器 */
.settings-container {
  max-width: 800px;
  margin: 0 auto;
}

.settings-card {
  background: rgba(255, 255, 255, 0.95);
  border-radius: 16px;
  border: none;
}

.settings-card :deep(.el-card__header) {
  border-bottom: 1px solid #e5e7eb;
  padding: 1.25rem 1.5rem;
}

.card-header {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.card-header .el-icon {
  font-size: 1.5rem;
  color: #667eea;
}

.card-header h2 {
  margin: 0;
  font-size: 1.25rem;
  font-weight: 600;
  color: #1a1a2e;
}

.settings-card :deep(.el-card__body) {
  padding: 1.5rem;
}

.settings-card :deep(.el-form-item__label) {
  font-weight: 500;
  color: #374151;
}
</style>
