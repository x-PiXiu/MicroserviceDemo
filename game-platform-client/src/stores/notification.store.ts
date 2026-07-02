import { defineStore } from 'pinia'
import { ref } from 'vue'
import { ElNotification } from 'element-plus'

export const useNotificationStore = defineStore('notification', () => {
  const notifications = ref<any[]>([])

  const show = (options: Omit<any, 'id'>) => {
    const id = Date.now()
    const notification = ElNotification({
      ...options,
      onClose: () => {
        notifications.value = notifications.value.filter((n) => n.id !== id)
      },
    })
    notifications.value.push({ ...options, id, notification })
  }

  const success = (message: string, title = '成功') => {
    show({ type: 'success', title, message })
  }

  const warning = (message: string, title = '警告') => {
    show({ type: 'warning', title, message })
  }

  const error = (message: string, title = '错误') => {
    show({ type: 'error', title, message })
  }

  const info = (message: string, title = '提示') => {
    show({ type: 'info', title, message })
  }

  const clear = () => {
    ElNotification.closeAll()
    notifications.value = []
  }

  return {
    notifications,
    show,
    success,
    warning,
    error,
    info,
    clear,
  }
})
