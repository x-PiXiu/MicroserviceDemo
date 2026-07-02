import { useAuthStore } from '@/stores'

export function useAuth() {
  const authStore = useAuthStore()

  return {
    user: authStore.user,
    isAuthenticated: authStore.isAuthenticated,
    userName: authStore.userName,
    loading: authStore.loading,
    login: authStore.login,
    register: authStore.register,
    logout: authStore.logout,
    validateSession: authStore.validateSession,
  }
}
