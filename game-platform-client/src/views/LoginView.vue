<template>
  <div class="login-page">
    <!-- 动态背景 -->
    <div class="bg-gradient"></div>
    <div class="bg-mesh"></div>
    <div class="particles" ref="particlesRef"></div>

    <div class="login-container">
      <div class="login-card">
        <!-- Logo 区域 -->
        <div class="card-header">
          <div class="logo-wrapper">
            <div class="logo-glow"></div>
            <svg viewBox="0 0 100 100" class="logo-icon">
              <defs>
                <linearGradient id="logoGrad" x1="0%" y1="0%" x2="100%" y2="100%">
                  <stop offset="0%" style="stop-color:#667eea"/>
                  <stop offset="100%" style="stop-color:#764ba2"/>
                </linearGradient>
              </defs>
              <circle cx="50" cy="30" r="15" class="piece-black" />
              <circle cx="30" cy="60" r="15" class="piece-white" />
              <circle cx="70" cy="60" r="15" class="piece-black" />
              <circle cx="50" cy="80" r="10" class="piece-white" />
            </svg>
          </div>
          <h1 class="title">{{ t('auth.login') }}</h1>
          <p class="subtitle">{{ t('auth.welcomeBack') || '欢迎回来，开始您的游戏之旅' }}</p>
        </div>

        <!-- 登录方式切换 -->
        <div class="login-type-tabs">
          <button
            type="button"
            :class="['tab-btn', { active: loginType === 'username' }]"
            @click="loginType = 'username'"
          >
            <el-icon><User /></el-icon>
            {{ t('auth.usernameLogin') || '用户名登录' }}
          </button>
          <button
            type="button"
            :class="['tab-btn', { active: loginType === 'email' }]"
            @click="loginType = 'email'"
          >
            <el-icon><Message /></el-icon>
            {{ t('auth.emailLogin') || '邮箱登录' }}
          </button>
        </div>

        <el-form :model="loginType === 'username' ? form : emailForm" :rules="currentRules" ref="formRef" @submit.prevent="handleSubmit" class="login-form">
          <!-- 用户名登录 -->
          <template v-if="loginType === 'username'">
            <el-form-item prop="username">
              <div class="input-wrapper">
                <el-icon class="input-icon"><User /></el-icon>
                <el-input
                  v-model="form.username"
                  :placeholder="t('auth.username')"
                  size="large"
                  class="custom-input"
                />
              </div>
            </el-form-item>

            <el-form-item prop="password">
              <div class="input-wrapper">
                <el-icon class="input-icon"><Lock /></el-icon>
                <el-input
                  v-model="form.password"
                  type="password"
                  :placeholder="t('auth.password')"
                  size="large"
                  show-password
                  class="custom-input"
                  @keyup.enter="handleSubmit"
                />
              </div>
            </el-form-item>
          </template>

          <!-- 邮箱验证码登录 -->
          <template v-else>
            <el-form-item prop="email">
              <div class="input-wrapper">
                <el-icon class="input-icon"><Message /></el-icon>
                <el-input
                  v-model="emailForm.email"
                  :placeholder="t('auth.email') || '请输入邮箱'"
                  size="large"
                  class="custom-input"
                />
              </div>
            </el-form-item>

            <el-form-item prop="code">
              <div class="input-wrapper code-wrapper">
                <el-icon class="input-icon"><Key /></el-icon>
                <el-input
                  v-model="emailForm.code"
                  :placeholder="t('auth.verificationCode') || '验证码'"
                  size="large"
                  maxlength="6"
                  class="custom-input"
                  @keyup.enter="handleSubmit"
                />
                <el-button
                  class="send-code-btn"
                  :disabled="isSendingCode || emailCooldown > 0"
                  @click="handleSendLoginCode"
                >
                  <span v-if="emailCooldown > 0">{{ emailCooldown }}s</span>
                  <span v-else>{{ t('auth.sendCode') || '获取验证码' }}</span>
                </el-button>
              </div>
            </el-form-item>
          </template>

          <el-form-item class="options-row">
            <el-checkbox v-if="loginType === 'username'" v-model="rememberMe">
              {{ t('auth.rememberMe') || '记住我' }}
            </el-checkbox>
            <span v-else></span>
            <router-link to="/forgot-password" class="forgot-link">
              {{ t('auth.forgotPassword') || '忘记密码？' }}
            </router-link>
          </el-form-item>

          <el-form-item>
            <button type="submit" class="login-btn" :class="{ loading: isLoading }" :disabled="isLoading">
              <span v-if="!isLoading" class="btn-text">
                {{ loginType === 'username' ? t('auth.loginBtn') : t('auth.emailLoginBtn') }}
              </span>
              <span v-else class="btn-loading">
                <span class="dot"></span>
                <span class="dot"></span>
                <span class="dot"></span>
              </span>
            </button>
          </el-form-item>
        </el-form>

        <div class="divider">
          <span class="divider-line"></span>
          <span class="divider-text">{{ t('auth.or') || '其他方式' }}</span>
          <span class="divider-line"></span>
        </div>

        <div class="social-login">
          <button class="social-btn" type="button">
            <svg viewBox="0 0 24 24" width="20" height="20">
              <path fill="currentColor" d="M12 2C6.477 2 2 6.477 2 12c0 4.991 3.657 9.128 8.438 9.879V14.89h-2.54V12h2.54V9.797c0-2.506 1.492-3.89 3.777-3.89 1.094 0 2.238.195 2.238.195v2.46h-1.26c-1.243 0-1.63.771-1.63 1.562V12h2.773l-.443 2.89h-2.33v6.989C18.343 21.129 22 16.99 22 12c0-5.523-4.477-10-10-10z"/>
            </svg>
          </button>
          <button class="social-btn" type="button">
            <svg viewBox="0 0 24 24" width="20" height="20">
              <path fill="currentColor" d="M12.545,10.239v3.821h5.445c-0.712,2.315-2.647,3.972-5.445,3.972c-3.332,0-6.033-2.701-6.033-6.032s2.701-6.032,6.033-6.032c1.498,0,2.866,0.549,3.921,1.453l2.814-2.814C17.503,2.988,15.139,2,12.545,2C7.021,2,2.543,6.477,2.543,12s4.478,10,10.002,10c8.396,0,10.249-7.85,9.426-11.748L12.545,10.239z"/>
            </svg>
          </button>
          <button class="social-btn" type="button">
            <svg viewBox="0 0 24 24" width="20" height="20">
              <path fill="currentColor" d="M18.244 2.25h3.308l-7.227 8.26 8.502 11.24H16.17l-5.214-6.817L4.99 21.75H1.68l7.73-8.835L1.254 2.25H8.08l4.713 6.231zm-1.161 17.52h1.833L7.084 4.126H5.117z"/>
            </svg>
          </button>
        </div>

        <div class="card-footer">
          <p>
            {{ t('auth.noAccount') || '还没有账号？' }}
            <router-link to="/register" class="register-link">{{ t('auth.register') }}</router-link>
          </p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted, onUnmounted, computed } from 'vue'
import { useRoute } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { ElMessage } from 'element-plus'
import { User, Lock, Message, Key } from '@element-plus/icons-vue'
import { useAuthStore } from '@/stores'
import { logger } from '@/utils/logger'
import { authApi } from '@/api/auth.api'
import type { FormInstance, FormRules } from 'element-plus'
import { validators, errorMessages } from '@/utils'

const { t } = useI18n()
const route = useRoute()
const authStore = useAuthStore()

const isLoading = computed(() => authStore.loading)
const loginType = ref<'username' | 'email'>('username')

const formRef = ref<FormInstance>()
const rememberMe = ref(false)
const form = reactive({
  username: '',
  password: '',
})

const emailForm = reactive({
  email: '',
  code: '',
})
const isSendingCode = ref(false)
const emailCooldown = ref(0)
let cooldownTimer: ReturnType<typeof setInterval> | null = null

// 动态规则
const currentRules = computed<FormRules>(() => {
  if (loginType.value === 'username') {
    return usernameRules
  }
  return emailRules
})

const usernameRules: FormRules = {
  username: [
    { required: true, message: errorMessages.username, trigger: 'blur' },
  ],
  password: [
    { required: true, message: errorMessages.password, trigger: 'blur' },
  ],
}

const emailRules: FormRules = {
  email: [
    { required: true, message: errorMessages.email, trigger: 'blur' },
    {
      validator: (_rule: unknown, value: string, callback: (error?: Error) => void) => {
        if (validators.email(value)) {
          callback()
        } else {
          callback(new Error(errorMessages.email))
        }
      },
      trigger: 'blur',
    },
  ],
  code: [
    { required: true, message: t('auth.codeRequired') || '请输入验证码', trigger: 'blur' },
  ],
}

onMounted(() => {
  if (route.query.username) {
    form.username = route.query.username as string
  }
  if (route.query.password) {
    form.password = route.query.password as string
  }
})

onUnmounted(() => {
  if (cooldownTimer) clearInterval(cooldownTimer)
})

const handleSubmit = async () => {
  if (loginType.value === 'username') {
    await handleLogin()
  } else {
    await handleEmailLogin()
  }
}

const handleLogin = async () => {
  if (!formRef.value) return

  try {
    const valid = await formRef.value.validate().catch(() => false)
    if (!valid) return

    await authStore.login(form)
  } catch (error: unknown) {
    if (error instanceof Error) {
      ElMessage.error(error.message || '登录失败')
    }
  }
}

const handleSendLoginCode = async () => {
  if (!validators.email(emailForm.email)) {
    ElMessage.error(errorMessages.email)
    return
  }

  try {
    isSendingCode.value = true
    const response = await authApi.sendVerifyCode({
      email: emailForm.email,
      purpose: 'login',
    })
    startEmailCooldown(response.cooldown_seconds || 60)
    ElMessage.success(response.message || '验证码已发送')
  } catch (error: unknown) {
    if (error instanceof Error) {
      ElMessage.error(error.message || '发送验证码失败')
    }
  } finally {
    isSendingCode.value = false
  }
}

const handleEmailLogin = async () => {
  if (!validators.email(emailForm.email)) {
    ElMessage.error(errorMessages.email)
    return
  }
  if (!emailForm.code || emailForm.code.length !== 6) {
    ElMessage.error(t('auth.codeLength') || '验证码为6位数字')
    return
  }

  try {
    await authStore.login({
      email: emailForm.email,
      code: emailForm.code,
    })
  } catch (error: unknown) {
    if (error instanceof Error) {
      ElMessage.error(error.message || '登录失败')
    }
  }
}

const startEmailCooldown = (seconds: number) => {
  emailCooldown.value = seconds
  if (cooldownTimer) clearInterval(cooldownTimer)
  cooldownTimer = setInterval(() => {
    emailCooldown.value--
    if (emailCooldown.value <= 0) {
      emailCooldown.value = 0
      if (cooldownTimer) {
        clearInterval(cooldownTimer)
        cooldownTimer = null
      }
    }
  }, 1000)
}
</script>

<style scoped>
.login-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  overflow: hidden;
  background: #0a0a0f;
}

/* 渐变背景 */
.bg-gradient {
  position: absolute;
  inset: 0;
  background:
    radial-gradient(ellipse at 20% 20%, rgba(102, 126, 234, 0.15) 0%, transparent 50%),
    radial-gradient(ellipse at 80% 80%, rgba(118, 75, 162, 0.15) 0%, transparent 50%),
    radial-gradient(ellipse at 50% 50%, rgba(102, 126, 234, 0.05) 0%, transparent 70%);
  animation: gradientShift 15s ease-in-out infinite;
}

@keyframes gradientShift {
  0%, 100% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.8; transform: scale(1.1); }
}

/* 网格背景 */
.bg-mesh {
  position: absolute;
  inset: 0;
  background-image:
    linear-gradient(rgba(102, 126, 234, 0.03) 1px, transparent 1px),
    linear-gradient(90deg, rgba(102, 126, 234, 0.03) 1px, transparent 1px);
  background-size: 50px 50px;
  mask-image: radial-gradient(ellipse at center, black 20%, transparent 70%);
}

/* 粒子效果 */
.particles {
  position: absolute;
  inset: 0;
  overflow: hidden;
}

.login-container {
  position: relative;
  z-index: 10;
  width: 100%;
  max-width: 420px;
  padding: 1rem;
}

.login-card {
  background: rgba(15, 15, 25, 0.8);
  backdrop-filter: blur(20px);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 24px;
  padding: 2.5rem 2rem;
  box-shadow:
    0 25px 50px -12px rgba(0, 0, 0, 0.5),
    0 0 0 1px rgba(255, 255, 255, 0.05) inset;
}

/* Logo */
.card-header {
  text-align: center;
  margin-bottom: 2rem;
}

.logo-wrapper {
  position: relative;
  display: inline-block;
  margin-bottom: 1.5rem;
}

.logo-glow {
  position: absolute;
  inset: -20px;
  background: radial-gradient(circle, rgba(102, 126, 234, 0.4) 0%, transparent 70%);
  filter: blur(15px);
  animation: pulse 3s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 0.5; transform: scale(1); }
  50% { opacity: 0.8; transform: scale(1.1); }
}

.logo-icon {
  width: 70px;
  height: 70px;
  position: relative;
  filter: drop-shadow(0 4px 12px rgba(102, 126, 234, 0.3));
}

.piece-black { fill: #667eea; }
.piece-white { fill: #f5f5f5; stroke: #667eea; stroke-width: 2; }

.title {
  font-size: 1.75rem;
  font-weight: 700;
  color: #ffffff;
  margin-bottom: 0.5rem;
  letter-spacing: -0.02em;
}

.subtitle {
  color: rgba(255, 255, 255, 0.5);
  font-size: 0.9rem;
}

/* 登录方式切换 */
.login-type-tabs {
  display: flex;
  gap: 0;
  margin-bottom: 1.5rem;
  background: rgba(255, 255, 255, 0.03);
  border-radius: 12px;
  padding: 4px;
  border: 1px solid rgba(255, 255, 255, 0.05);
}

.tab-btn {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.5rem;
  padding: 0.75rem 1rem;
  border: none;
  background: transparent;
  border-radius: 8px;
  font-size: 0.85rem;
  font-weight: 500;
  color: rgba(255, 255, 255, 0.4);
  cursor: pointer;
  transition: all 0.3s ease;
}

.tab-btn.active {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4);
}

.tab-btn:hover:not(.active) {
  color: rgba(255, 255, 255, 0.7);
}

/* 表单 */
.login-form {
  margin-top: 0.5rem;
}

.input-wrapper {
  display: flex;
  align-items: center;
  width: 100%;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 12px;
  padding: 0 1rem;
  transition: all 0.3s ease;
}

.input-wrapper:focus-within {
  border-color: #667eea;
  background: rgba(102, 126, 234, 0.05);
  box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.15);
}

.input-icon {
  color: rgba(255, 255, 255, 0.3);
  font-size: 1.1rem;
  margin-right: 0.75rem;
}

.custom-input {
  flex: 1;
}

.custom-input :deep(.el-input__wrapper) {
  background: transparent !important;
  box-shadow: none !important;
  padding: 0 !important;
}

.custom-input :deep(.el-input__inner) {
  color: #ffffff;
  font-size: 0.95rem;
}

.custom-input :deep(.el-input__inner::placeholder) {
  color: rgba(255, 255, 255, 0.3);
}

.code-wrapper {
  padding-right: 0.5rem;
}

.send-code-btn {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  border: none;
  color: white;
  font-size: 0.75rem;
  padding: 0.5rem 0.75rem;
  border-radius: 8px;
  cursor: pointer;
  white-space: nowrap;
  min-width: 90px;
  transition: all 0.3s ease;
}

.send-code-btn:hover:not(:disabled) {
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(102, 126, 234, 0.4);
}

.send-code-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.options-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin: 1rem 0 1.5rem;
}

.options-row :deep(.el-form-item__content) {
  justify-content: space-between;
}

.options-row :deep(.el-checkbox__label) {
  color: rgba(255, 255, 255, 0.5);
  font-size: 0.85rem;
}

.options-row :deep(.el-checkbox__input.is-checked .el-checkbox__inner) {
  background: #667eea;
  border-color: #667eea;
}

.forgot-link {
  color: #667eea;
  text-decoration: none;
  font-size: 0.85rem;
  transition: color 0.3s;
}

.forgot-link:hover {
  color: #764ba2;
}

/* 登录按钮 */
.login-btn {
  width: 100%;
  height: 52px;
  border: none;
  border-radius: 12px;
  font-size: 1rem;
  font-weight: 600;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  cursor: pointer;
  transition: all 0.3s ease;
  position: relative;
  overflow: hidden;
}

.login-btn::before {
  content: '';
  position: absolute;
  inset: 0;
  background: linear-gradient(135deg, rgba(255,255,255,0.2) 0%, transparent 50%);
  opacity: 0;
  transition: opacity 0.3s;
}

.login-btn:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 8px 25px rgba(102, 126, 234, 0.5);
}

.login-btn:hover::before {
  opacity: 1;
}

.login-btn:active:not(:disabled) {
  transform: translateY(0);
}

.login-btn:disabled {
  cursor: not-allowed;
  opacity: 0.7;
}

.btn-loading {
  display: flex;
  justify-content: center;
  align-items: center;
  gap: 4px;
}

.btn-loading .dot {
  width: 6px;
  height: 6px;
  background: white;
  border-radius: 50%;
  animation: bounce 1.4s ease-in-out infinite;
}

.btn-loading .dot:nth-child(1) { animation-delay: 0s; }
.btn-loading .dot:nth-child(2) { animation-delay: 0.2s; }
.btn-loading .dot:nth-child(3) { animation-delay: 0.4s; }

@keyframes bounce {
  0%, 80%, 100% { transform: scale(0.6); opacity: 0.5; }
  40% { transform: scale(1); opacity: 1; }
}

/* 分隔符 */
.divider {
  display: flex;
  align-items: center;
  margin: 1.5rem 0;
  gap: 1rem;
}

.divider-line {
  flex: 1;
  height: 1px;
  background: linear-gradient(90deg, transparent, rgba(255,255,255,0.1), transparent);
}

.divider-text {
  color: rgba(255, 255, 255, 0.3);
  font-size: 0.75rem;
  text-transform: uppercase;
  letter-spacing: 0.1em;
}

/* 社交登录 */
.social-login {
  display: flex;
  justify-content: center;
  gap: 1rem;
}

.social-btn {
  width: 48px;
  height: 48px;
  border-radius: 12px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  background: rgba(255, 255, 255, 0.03);
  color: rgba(255, 255, 255, 0.6);
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.3s ease;
}

.social-btn:hover {
  border-color: #667eea;
  color: #667eea;
  background: rgba(102, 126, 234, 0.1);
  transform: translateY(-2px);
}

/* 底部链接 */
.card-footer {
  text-align: center;
  margin-top: 1.5rem;
  padding-top: 1.5rem;
  border-top: 1px solid rgba(255, 255, 255, 0.05);
}

.card-footer p {
  color: rgba(255, 255, 255, 0.5);
  font-size: 0.9rem;
  margin: 0;
}

.register-link {
  color: #667eea;
  text-decoration: none;
  font-weight: 600;
  margin-left: 0.25rem;
  transition: color 0.3s;
}

.register-link:hover {
  color: #764ba2;
}

/* 响应式 */
@media (max-width: 480px) {
  .login-card {
    padding: 2rem 1.5rem;
    border-radius: 20px;
  }

  .title {
    font-size: 1.5rem;
  }

  .tab-btn {
    font-size: 0.8rem;
    padding: 0.6rem 0.5rem;
  }

  .tab-btn .el-icon {
    display: none;
  }
}
</style>
