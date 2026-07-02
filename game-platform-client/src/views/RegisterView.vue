<template>
  <div class="register-page">
    <!-- 动态背景 -->
    <div class="bg-gradient"></div>
    <div class="bg-mesh"></div>

    <div class="register-container">
      <div class="register-card">
        <!-- Logo 区域 -->
        <div class="card-header">
          <div class="logo-wrapper">
            <div class="logo-glow"></div>
            <svg viewBox="0 0 100 100" class="logo-icon">
              <defs>
                <linearGradient id="logoGradReg" x1="0%" y1="0%" x2="100%" y2="100%">
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
          <h1 class="title">{{ t('auth.register') }}</h1>
          <p class="step-indicator">
            <span :class="['step-dot', { active: currentStep === 'info', done: currentStep !== 'info' }]"></span>
            {{ t('auth.fillUserInfo') || '填写信息' }}
            <span class="step-arrow">→</span>
            <span :class="['step-dot', { active: currentStep === 'verify' }]"></span>
            {{ t('auth.verifyEmail') || '验证邮箱' }}
            <span class="step-arrow">→</span>
            <span :class="['step-dot', { active: currentStep === 'success' }]"></span>
            {{ t('auth.done') || '完成' }}
          </p>
        </div>

        <!-- Step 1: 填写用户信息 -->
        <el-form
          v-if="currentStep === 'info'"
          :model="form"
          :rules="infoRules"
          ref="formRef"
          @submit.prevent="handleSendCode"
          class="register-form"
        >
          <el-form-item prop="email">
            <div class="input-wrapper">
              <el-icon class="input-icon"><Message /></el-icon>
              <el-input
                v-model="form.email"
                :placeholder="t('auth.email')"
                size="large"
                class="custom-input"
              />
            </div>
          </el-form-item>

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
              />
            </div>
          </el-form-item>

          <el-form-item prop="confirmPassword">
            <div class="input-wrapper">
              <el-icon class="input-icon"><Lock /></el-icon>
              <el-input
                v-model="form.confirmPassword"
                type="password"
                :placeholder="t('auth.confirmPassword')"
                size="large"
                show-password
                class="custom-input"
                @keyup.enter="handleSendCode"
              />
            </div>
          </el-form-item>

          <el-form-item>
            <button type="submit" class="register-btn" :disabled="isSendingCode">
              <span v-if="!isSendingCode">{{ t('auth.sendCode') || '发送验证码' }}</span>
              <span v-else class="btn-loading">
                <span class="dot"></span>
                <span class="dot"></span>
                <span class="dot"></span>
              </span>
            </button>
          </el-form-item>
        </el-form>

        <!-- Step 2: 验证邮箱 -->
        <el-form
          v-if="currentStep === 'verify'"
          :model="verifyForm"
          :rules="verifyRules"
          ref="verifyFormRef"
          @submit.prevent="handleVerify"
          class="register-form"
        >
          <div class="verify-tip">
            <span class="tip-text">{{ t('auth.codeSentTo') || '验证码已发送至' }}</span>
            <span class="tip-email">{{ form.email }}</span>
            <button type="button" class="modify-btn" @click="goBackToInfo">{{ t('auth.modify') || '修改' }}</button>
          </div>

          <el-form-item prop="code">
            <div class="input-wrapper">
              <el-icon class="input-icon"><Key /></el-icon>
              <el-input
                v-model="verifyForm.code"
                :placeholder="t('auth.enterCode') || '请输入6位验证码'"
                size="large"
                maxlength="6"
                class="custom-input"
                @keyup.enter="handleVerify"
              />
            </div>
          </el-form-item>

          <div class="cooldown-tip" v-if="cooldownSeconds > 0">
            {{ t('auth.waitCooldown') || `请等待 ${cooldownSeconds} 秒后重新发送` }}
          </div>

          <el-form-item>
            <button type="submit" class="register-btn" :disabled="isVerifying">
              <span v-if="!isVerifying">{{ t('auth.verifyAndRegister') || '验证并注册' }}</span>
              <span v-else class="btn-loading">
                <span class="dot"></span>
                <span class="dot"></span>
                <span class="dot"></span>
              </span>
            </button>
          </el-form-item>

          <div class="resend-tip">
            {{ t('auth.notReceived') || '没有收到验证码？' }}
            <button type="button" class="resend-btn" :disabled="cooldownSeconds > 0" @click="handleResendCode">
              {{ t('auth.resend') || '重新发送' }}
            </button>
          </div>
        </el-form>

        <!-- Step 3: 注册成功 -->
        <div v-if="currentStep === 'success'" class="success-step">
          <div class="success-icon-wrapper">
            <div class="success-glow"></div>
            <el-icon class="success-icon"><CircleCheck /></el-icon>
          </div>
          <h2 class="success-title">{{ t('auth.registerSuccess') || '注册成功！' }}</h2>
          <p class="success-message">{{ t('auth.welcome') || '欢迎加入，开始您的游戏之旅' }}</p>
          <button class="register-btn" @click="goToLogin">
            {{ t('auth.goToLogin') || '前往登录' }}
          </button>
        </div>

        <div class="card-footer">
          <p>
            {{ t('auth.hasAccount') || '已有账号？' }}
            <router-link to="/login" class="login-link">{{ t('auth.login') }}</router-link>
          </p>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onUnmounted } from 'vue'
import { useI18n } from 'vue-i18n'
import { useRouter } from 'vue-router'
import { ElMessage } from 'element-plus'
import { User, Lock, Message, Key, CircleCheck } from '@element-plus/icons-vue'
import { useAuthStore } from '@/stores'
import { logger } from '@/utils/logger'
import { authApi } from '@/api/auth.api'
import type { FormInstance, FormRules } from 'element-plus'
import { validators, errorMessages } from '@/utils'

const { t } = useI18n()
const router = useRouter()
const authStore = useAuthStore()

const currentStep = ref<'info' | 'verify' | 'success'>('info')
const isSendingCode = ref(false)
const isVerifying = ref(false)
const cooldownSeconds = ref(0)
let cooldownTimer: ReturnType<typeof setInterval> | null = null
const verifyToken = ref('')

const formRef = ref<FormInstance>()
const verifyFormRef = ref<FormInstance>()
const form = reactive({
  email: '',
  username: '',
  password: '',
  confirmPassword: '',
})
const verifyForm = reactive({
  code: '',
})

const validateConfirmPassword = (_rule: unknown, value: string, callback: (error?: Error) => void) => {
  if (value === '') {
    callback(new Error(t('auth.confirmPasswordRequired') || '请再次输入密码'))
  } else if (value !== form.password) {
    callback(new Error(t('auth.passwordMismatch') || '两次输入的密码不一致'))
  } else {
    callback()
  }
}

const infoRules: FormRules = {
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
  username: [
    { required: true, message: errorMessages.username, trigger: 'blur' },
  ],
  password: [
    { required: true, message: errorMessages.password, trigger: 'blur' },
    {
      validator: (_rule: unknown, value: string, callback: (error?: Error) => void) => {
        if (validators.password(value)) {
          callback()
        } else {
          callback(new Error(errorMessages.password))
        }
      },
      trigger: 'blur',
    },
  ],
  confirmPassword: [
    { required: true, validator: validateConfirmPassword, trigger: 'blur' },
  ],
}

const verifyRules: FormRules = {
  code: [
    { required: true, message: t('auth.codeRequired') || '请输入验证码', trigger: 'blur' },
    { len: 6, message: t('auth.codeLength') || '验证码为6位数字', trigger: 'blur' },
  ],
}

const handleSendCode = async () => {
  if (!formRef.value) return

  try {
    // 只验证邮箱和用户名，密码在第二步验证
    const valid = await formRef.value.validateField(['email', 'username']).catch(() => false)
    if (!valid) return

    isSendingCode.value = true
    const response = await authApi.sendVerifyCode({
      email: form.email,
      purpose: 'register',
    })

    currentStep.value = 'verify'
    startCooldown(response.cooldown_seconds || 60)
    ElMessage.success(response.message || '验证码已发送')
  } catch (error: unknown) {
    if (error instanceof Error) {
      ElMessage.error(error.message || '发送验证码失败')
    }
  } finally {
    isSendingCode.value = false
  }
}

const handleResendCode = async () => {
  if (cooldownSeconds.value > 0) return
  await handleSendCode()
}

const handleVerify = async () => {
  if (!verifyFormRef.value) return

  try {
    const valid = await verifyFormRef.value.validate().catch(() => false)
    if (!valid) return

    isVerifying.value = true

    const verifyResponse = await authApi.verifyCode({
      email: form.email,
      code: verifyForm.code,
      purpose: 'register',
    })

    if (verifyResponse.verify_token) {
      verifyToken.value = verifyResponse.verify_token
    }

    await authStore.register({
      username: form.username,
      password: form.password,
      email: form.email,
      verify_token: verifyToken.value,
    })

    currentStep.value = 'success'
    ElMessage.success(t('auth.registerSuccess') || '注册成功！')
  } catch (error: unknown) {
    if (error instanceof Error) {
      ElMessage.error(error.message || '注册失败，请重试')
    }
  } finally {
    isVerifying.value = false
  }
}

const goBackToInfo = () => {
  currentStep.value = 'info'
  verifyForm.code = ''
}

const goToLogin = () => {
  router.push({
    path: '/login',
    query: { username: form.username },
  })
}

const startCooldown = (seconds: number) => {
  cooldownSeconds.value = seconds
  if (cooldownTimer) clearInterval(cooldownTimer)
  cooldownTimer = setInterval(() => {
    cooldownSeconds.value--
    if (cooldownSeconds.value <= 0) {
      cooldownSeconds.value = 0
      if (cooldownTimer) {
        clearInterval(cooldownTimer)
        cooldownTimer = null
      }
    }
  }, 1000)
}

onUnmounted(() => {
  if (cooldownTimer) clearInterval(cooldownTimer)
})
</script>

<style scoped>
.register-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  position: relative;
  overflow: hidden;
  background: #0a0a0f;
}

.bg-gradient {
  position: absolute;
  inset: 0;
  background:
    radial-gradient(ellipse at 20% 80%, rgba(102, 126, 234, 0.15) 0%, transparent 50%),
    radial-gradient(ellipse at 80% 20%, rgba(118, 75, 162, 0.15) 0%, transparent 50%),
    radial-gradient(ellipse at 50% 50%, rgba(102, 126, 234, 0.05) 0%, transparent 70%);
  animation: gradientShift 15s ease-in-out infinite;
}

@keyframes gradientShift {
  0%, 100% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.8; transform: scale(1.1); }
}

.bg-mesh {
  position: absolute;
  inset: 0;
  background-image:
    linear-gradient(rgba(102, 126, 234, 0.03) 1px, transparent 1px),
    linear-gradient(90deg, rgba(102, 126, 234, 0.03) 1px, transparent 1px);
  background-size: 50px 50px;
  mask-image: radial-gradient(ellipse at center, black 20%, transparent 70%);
}

.register-container {
  position: relative;
  z-index: 10;
  width: 100%;
  max-width: 420px;
  padding: 1rem;
}

.register-card {
  background: rgba(15, 15, 25, 0.8);
  backdrop-filter: blur(20px);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 24px;
  padding: 2.5rem 2rem;
  box-shadow:
    0 25px 50px -12px rgba(0, 0, 0, 0.5),
    0 0 0 1px rgba(255, 255, 255, 0.05) inset;
}

.card-header {
  text-align: center;
  margin-bottom: 2rem;
}

.logo-wrapper {
  position: relative;
  display: inline-block;
  margin-bottom: 1rem;
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
  width: 60px;
  height: 60px;
  position: relative;
  filter: drop-shadow(0 4px 12px rgba(102, 126, 234, 0.3));
}

.piece-black { fill: #667eea; }
.piece-white { fill: #f5f5f5; stroke: #667eea; stroke-width: 2; }

.title {
  font-size: 1.75rem;
  font-weight: 700;
  color: #ffffff;
  margin: 0 0 0.75rem 0;
  letter-spacing: -0.02em;
}

.step-indicator {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.5rem;
  color: rgba(255, 255, 255, 0.4);
  font-size: 0.8rem;
  margin: 0;
}

.step-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.2);
  transition: all 0.3s;
}

.step-dot.active {
  background: #667eea;
  box-shadow: 0 0 8px rgba(102, 126, 234, 0.6);
}

.step-dot.done {
  background: #67c23a;
}

.step-arrow {
  color: rgba(255, 255, 255, 0.2);
  font-size: 0.7rem;
}

.register-form {
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

.verify-tip {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.5rem;
  padding: 0.875rem;
  background: rgba(102, 126, 234, 0.1);
  border: 1px solid rgba(102, 126, 234, 0.2);
  border-radius: 12px;
  margin-bottom: 1.25rem;
  font-size: 0.85rem;
}

.tip-text {
  color: rgba(255, 255, 255, 0.6);
}

.tip-email {
  color: #667eea;
  font-weight: 500;
}

.modify-btn {
  background: none;
  border: none;
  color: #667eea;
  font-size: 0.8rem;
  cursor: pointer;
  text-decoration: underline;
  padding: 0;
}

.modify-btn:hover {
  color: #764ba2;
}

.cooldown-tip {
  text-align: center;
  color: rgba(255, 255, 255, 0.4);
  font-size: 0.8rem;
  margin-bottom: 1rem;
}

.resend-tip {
  text-align: center;
  color: rgba(255, 255, 255, 0.4);
  font-size: 0.85rem;
  margin-top: 1rem;
}

.resend-btn {
  background: none;
  border: none;
  color: #667eea;
  font-size: 0.85rem;
  cursor: pointer;
  text-decoration: underline;
  padding: 0;
  margin-left: 0.25rem;
}

.resend-btn:hover:not(:disabled) {
  color: #764ba2;
}

.resend-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.register-btn {
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

.register-btn::before {
  content: '';
  position: absolute;
  inset: 0;
  background: linear-gradient(135deg, rgba(255,255,255,0.2) 0%, transparent 50%);
  opacity: 0;
  transition: opacity 0.3s;
}

.register-btn:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 8px 25px rgba(102, 126, 234, 0.5);
}

.register-btn:hover::before {
  opacity: 1;
}

.register-btn:active:not(:disabled) {
  transform: translateY(0);
}

.register-btn:disabled {
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

/* 成功步骤 */
.success-step {
  text-align: center;
  padding: 1.5rem 0;
}

.success-icon-wrapper {
  position: relative;
  display: inline-block;
  margin-bottom: 1.5rem;
}

.success-glow {
  position: absolute;
  inset: -30px;
  background: radial-gradient(circle, rgba(103, 194, 58, 0.4) 0%, transparent 70%);
  filter: blur(20px);
  animation: successPulse 2s ease-in-out infinite;
}

@keyframes successPulse {
  0%, 100% { opacity: 0.5; transform: scale(1); }
  50% { opacity: 0.8; transform: scale(1.2); }
}

.success-icon {
  font-size: 5rem;
  color: #67c23a;
  position: relative;
}

.success-title {
  font-size: 1.5rem;
  font-weight: 700;
  color: #ffffff;
  margin: 0 0 0.5rem 0;
}

.success-message {
  color: rgba(255, 255, 255, 0.5);
  font-size: 0.9rem;
  margin: 0 0 1.5rem 0;
}

.success-step .register-btn {
  background: linear-gradient(135deg, #67c23a 0%, #5daf34 100%);
}

.success-step .register-btn:hover {
  box-shadow: 0 8px 25px rgba(103, 194, 58, 0.4);
}

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

.login-link {
  color: #667eea;
  text-decoration: none;
  font-weight: 600;
  margin-left: 0.25rem;
  transition: color 0.3s;
}

.login-link:hover {
  color: #764ba2;
}

@media (max-width: 480px) {
  .register-card {
    padding: 2rem 1.5rem;
    border-radius: 20px;
  }

  .title {
    font-size: 1.5rem;
  }

  .step-indicator {
    font-size: 0.7rem;
  }
}
</style>
