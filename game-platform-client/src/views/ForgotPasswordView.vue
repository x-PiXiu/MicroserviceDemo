<template>
  <div class="forgot-page">
    <div class="bg-gradient"></div>
    <div class="bg-mesh"></div>

    <div class="forgot-container">
      <div class="forgot-card">
        <div class="card-header">
          <div class="logo-wrapper">
            <div class="logo-glow"></div>
            <svg viewBox="0 0 100 100" class="logo-icon">
              <defs>
                <linearGradient id="logoGradFp" x1="0%" y1="0%" x2="100%" y2="100%">
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
          <h1 class="title">{{ t('auth.resetPassword') || '重置密码' }}</h1>
          <p class="step-indicator">
            <span :class="['step-dot', { active: currentStep === 'email', done: currentStep !== 'email' }]"></span>
            {{ t('auth.enterEmail') || '输入邮箱' }}
            <span class="step-arrow">→</span>
            <span :class="['step-dot', { active: currentStep === 'verify' }]"></span>
            {{ t('auth.verify') || '验证' }}
            <span class="step-arrow">→</span>
            <span :class="['step-dot', { active: currentStep === 'success' }]"></span>
            {{ t('auth.done') || '完成' }}
          </p>
        </div>

        <!-- Step 1: 输入邮箱 -->
        <el-form
          v-if="currentStep === 'email'"
          :model="emailForm"
          :rules="emailRules"
          ref="emailFormRef"
          @submit.prevent="handleSendCode"
          class="forgot-form"
        >
          <el-form-item prop="email">
            <div class="input-wrapper">
              <el-icon class="input-icon"><Message /></el-icon>
              <el-input
                v-model="emailForm.email"
                :placeholder="t('auth.email') || '请输入注册邮箱'"
                size="large"
                class="custom-input"
              />
            </div>
          </el-form-item>

          <el-form-item>
            <button type="submit" class="action-btn" :disabled="isSendingCode">
              <span v-if="!isSendingCode">{{ t('auth.sendCode') || '发送验证码' }}</span>
              <span v-else class="btn-loading">
                <span class="dot"></span>
                <span class="dot"></span>
                <span class="dot"></span>
              </span>
            </button>
          </el-form-item>
        </el-form>

        <!-- Step 2: 输入验证码和新密码 -->
        <el-form
          v-if="currentStep === 'verify'"
          :model="verifyForm"
          :rules="verifyRules"
          ref="verifyFormRef"
          @submit.prevent="handleVerify"
          class="forgot-form"
        >
          <el-form-item prop="code">
            <div class="input-wrapper">
              <el-icon class="input-icon"><Key /></el-icon>
              <el-input
                v-model="verifyForm.code"
                :placeholder="t('auth.verificationCode') || '请输入验证码'"
                size="large"
                maxlength="6"
                class="custom-input"
                @keyup.enter="handleVerify"
              />
            </div>
          </el-form-item>

          <el-form-item prop="newPassword">
            <div class="input-wrapper">
              <el-icon class="input-icon"><Lock /></el-icon>
              <el-input
                v-model="verifyForm.newPassword"
                type="password"
                :placeholder="t('auth.newPassword') || '请输入新密码'"
                size="large"
                show-password
                class="custom-input"
                @input="onPasswordInput"
              />
            </div>
            <div v-if="passwordFeedback.show" :class="['password-tips', passwordFeedback.level]">
              <div class="tip-title">{{ t('auth.passwordRequirements') || '密码要求' }}</div>
              <div
                v-for="(rule, idx) in passwordFeedback.rules"
                :key="idx"
                :class="['tip-item', { passed: rule.passed }]"
              >
                <span class="tip-icon">{{ rule.passed ? '✓' : '○' }}</span>
                <span>{{ rule.label }}</span>
              </div>
            </div>
          </el-form-item>

          <el-form-item prop="confirmPassword">
            <div class="input-wrapper">
              <el-icon class="input-icon"><Lock /></el-icon>
              <el-input
                v-model="verifyForm.confirmPassword"
                type="password"
                :placeholder="t('auth.confirmNewPassword') || '请再次输入新密码'"
                size="large"
                show-password
                class="custom-input"
                @keyup.enter="handleVerify"
              />
            </div>
          </el-form-item>

          <div class="cooldown-tip" v-if="cooldownSeconds > 0">
            {{ t('auth.waitCooldown') || `请等待 ${cooldownSeconds} 秒后重新发送` }}
          </div>

          <el-form-item>
            <button type="submit" class="action-btn" :disabled="isVerifying">
              <span v-if="!isVerifying">{{ t('auth.resetPassword') || '重置密码' }}</span>
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

        <!-- Step 3: 重置成功 -->
        <div v-if="currentStep === 'success'" class="success-step">
          <div class="success-icon-wrapper">
            <div class="success-glow"></div>
            <el-icon class="success-icon"><CircleCheck /></el-icon>
          </div>
          <h2 class="success-title">{{ t('auth.passwordResetSuccess') || '密码重置成功！' }}</h2>
          <p class="success-message">{{ t('auth.useNewPassword') || '请使用新密码登录' }}</p>
          <button class="action-btn success-btn" @click="goToLogin">
            {{ t('auth.goToLogin') || '前往登录' }}
          </button>
        </div>

        <div class="back-to-login">
          <router-link to="/login" class="back-link">
            <el-icon><ArrowLeft /></el-icon>
            {{ t('auth.backToLogin') || '返回登录' }}
          </router-link>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { useI18n } from 'vue-i18n'
import { ElMessage } from 'element-plus'
import { Message, Lock, Key, CircleCheck, ArrowLeft } from '@element-plus/icons-vue'
import { authApi } from '@/api/auth.api'
import type { FormInstance, FormRules } from 'element-plus'
import { validators, errorMessages, passwordChecker } from '@/utils'

const { t } = useI18n()
const router = useRouter()

const currentStep = ref<'email' | 'verify' | 'success'>('email')
const isSendingCode = ref(false)
const isVerifying = ref(false)
const cooldownSeconds = ref(0)
let cooldownTimer: ReturnType<typeof setInterval> | null = null
const savedEmail = ref('')

const emailFormRef = ref<FormInstance>()
const emailForm = reactive({
  email: '',
})

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
}

const verifyFormRef = ref<FormInstance>()
const verifyForm = reactive({
  code: '',
  newPassword: '',
  confirmPassword: '',
})

const passwordFeedback = reactive({
  show: false,
  rules: [] as { label: string; passed: boolean }[],
  level: '' as 'weak' | 'medium' | 'strong' | '',
})

const onPasswordInput = () => {
  const pwd = verifyForm.newPassword
  if (!pwd) {
    passwordFeedback.show = false
    passwordFeedback.level = ''
    passwordFeedback.rules = []
    return
  }
  passwordFeedback.show = true
  const result = passwordChecker.check(pwd)
  passwordFeedback.rules = result.rules
  passwordFeedback.level = result.level
}

const validateConfirmPassword = (_rule: unknown, value: string, callback: (error?: Error) => void) => {
  if (value === '') {
    callback(new Error(t('auth.confirmPasswordRequired') || '请再次输入密码'))
  } else if (value !== verifyForm.newPassword) {
    callback(new Error(t('auth.passwordMismatch') || '两次输入的密码不一致'))
  } else {
    callback()
  }
}

const verifyRules: FormRules = {
  code: [
    { required: true, message: t('auth.codeRequired') || '请输入验证码', trigger: 'blur' },
    { len: 6, message: t('auth.codeLength') || '验证码为6位数字', trigger: 'blur' },
  ],
  newPassword: [
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

const handleSendCode = async () => {
  if (!emailFormRef.value) return

  try {
    const valid = await emailFormRef.value.validate().catch(() => false)
    if (!valid) return

    isSendingCode.value = true
    const response = await authApi.sendVerifyCode({
      email: emailForm.email,
      purpose: 'reset_password',
    })

    savedEmail.value = emailForm.email
    currentStep.value = 'verify'
    startCooldown(response.cooldown_seconds || 60)
    ElMessage.success(response.message || '验证码已发送')
  } catch (error: unknown) {
    // 错误拦截器已展示具体错误信息，此处仅记录
    if (error instanceof Error) {
      console.error('发送验证码失败:', error.message)
    }
  } finally {
    isSendingCode.value = false
  }
}

const handleResendCode = async () => {
  if (cooldownSeconds.value > 0 || isSendingCode.value || !savedEmail.value) return

  try {
    isSendingCode.value = true
    const response = await authApi.sendVerifyCode({
      email: savedEmail.value,
      purpose: 'reset_password',
    })
    startCooldown(response.cooldown_seconds || 60)
    ElMessage.success(response.message || '验证码已重新发送')
  } catch (error: unknown) {
    if (error instanceof Error) {
      console.error('重新发送验证码失败:', error.message)
    }
  } finally {
    isSendingCode.value = false
  }
}

const handleVerify = async () => {
  if (!verifyFormRef.value) return

  try {
    const valid = await verifyFormRef.value.validate().catch(() => false)
    if (!valid) return

    isVerifying.value = true

    await authApi.resetPassword({
      email: savedEmail.value,
      code: verifyForm.code,
      new_password: verifyForm.newPassword,
    })

    currentStep.value = 'success'
    ElMessage.success('密码重置成功！')
  } catch (error: unknown) {
    // 拦截器已展示具体错误信息，此处仅记录
    if (error instanceof Error) {
      console.error('密码重置失败:', error.message)
    }
  } finally {
    isVerifying.value = false
  }
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

const goToLogin = () => {
  router.push('/login')
}

onUnmounted(() => {
  if (cooldownTimer) clearInterval(cooldownTimer)
})
</script>

<style scoped>
.forgot-page {
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
    radial-gradient(ellipse at 30% 30%, rgba(102, 126, 234, 0.12) 0%, transparent 50%),
    radial-gradient(ellipse at 70% 70%, rgba(118, 75, 162, 0.12) 0%, transparent 50%),
    radial-gradient(ellipse at 50% 50%, rgba(102, 126, 234, 0.05) 0%, transparent 70%);
  animation: gradientShift 15s ease-in-out infinite;
}

@keyframes gradientShift {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.8; }
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

.forgot-container {
  position: relative;
  z-index: 10;
  width: 100%;
  max-width: 420px;
  padding: 1rem;
}

.forgot-card {
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
  font-size: 1.5rem;
  font-weight: 700;
  color: #ffffff;
  margin: 0 0 0.75rem 0;
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

.forgot-form {
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

.cooldown-tip {
  text-align: center;
  color: rgba(255, 255, 255, 0.4);
  font-size: 0.8rem;
  margin-bottom: 1rem;
}

.password-tips {
  margin-top: 0.5rem;
  padding: 0.75rem;
  border-radius: 8px;
  border: 1px solid rgba(255, 255, 255, 0.06);
  transition: all 0.3s ease;
}

.password-tips.weak {
  background: rgba(245, 108, 108, 0.08);
  border-color: rgba(245, 108, 108, 0.2);
}

.password-tips.medium {
  background: rgba(230, 162, 60, 0.08);
  border-color: rgba(230, 162, 60, 0.2);
}

.password-tips.strong {
  background: rgba(103, 194, 58, 0.08);
  border-color: rgba(103, 194, 58, 0.2);
}

.tip-title {
  font-size: 0.75rem;
  color: rgba(255, 255, 255, 0.5);
  margin-bottom: 0.4rem;
  font-weight: 600;
}

.tip-item {
  display: flex;
  align-items: center;
  gap: 0.4rem;
  font-size: 0.78rem;
  color: rgba(255, 255, 255, 0.4);
  line-height: 1.8;
  transition: color 0.2s;
}

.tip-item.passed {
  color: rgba(103, 194, 58, 0.9);
}

.tip-icon {
  font-size: 0.7rem;
  width: 14px;
  text-align: center;
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

.action-btn {
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

.action-btn::before {
  content: '';
  position: absolute;
  inset: 0;
  background: linear-gradient(135deg, rgba(255,255,255,0.2) 0%, transparent 50%);
  opacity: 0;
  transition: opacity 0.3s;
}

.action-btn:hover:not(:disabled) {
  transform: translateY(-2px);
  box-shadow: 0 8px 25px rgba(102, 126, 234, 0.5);
}

.action-btn:hover::before {
  opacity: 1;
}

.action-btn:active:not(:disabled) {
  transform: translateY(0);
}

.action-btn:disabled {
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

.success-btn {
  background: linear-gradient(135deg, #67c23a 0%, #5daf34 100%);
}

.success-btn:hover {
  box-shadow: 0 8px 25px rgba(103, 194, 58, 0.4);
}

.back-to-login {
  text-align: center;
  margin-top: 1.5rem;
  padding-top: 1.5rem;
  border-top: 1px solid rgba(255, 255, 255, 0.05);
}

.back-link {
  display: inline-flex;
  align-items: center;
  gap: 0.5rem;
  color: rgba(255, 255, 255, 0.5);
  text-decoration: none;
  font-size: 0.9rem;
  transition: color 0.3s;
}

.back-link:hover {
  color: #667eea;
}

@media (max-width: 480px) {
  .forgot-card {
    padding: 2rem 1.5rem;
    border-radius: 20px;
  }

  .title {
    font-size: 1.3rem;
  }

  .step-indicator {
    font-size: 0.7rem;
  }
}
</style>
