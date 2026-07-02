const PREFIX = 'game_platform_'

export const storage = {
  get: <T>(key: string, defaultValue?: T): T | null => {
    try {
      const item = localStorage.getItem(`${PREFIX}${key}`)
      return item ? JSON.parse(item) : defaultValue ?? null
    } catch {
      return defaultValue ?? null
    }
  },

  set: (key: string, value: any): void => {
    try {
      localStorage.setItem(`${PREFIX}${key}`, JSON.stringify(value))
    } catch (error) {
      console.error('Storage set error:', error)
    }
  },

  remove: (key: string): void => {
    localStorage.removeItem(`${PREFIX}${key}`)
  },

  clear: (): void => {
    const keys = Object.keys(localStorage)
    keys.forEach((key) => {
      if (key.startsWith(PREFIX)) {
        localStorage.removeItem(key)
      }
    })
  },
}

export const session = {
  get: <T>(key: string, defaultValue?: T): T | null => {
    try {
      const item = sessionStorage.getItem(`${PREFIX}${key}`)
      return item ? JSON.parse(item) : defaultValue ?? null
    } catch {
      return defaultValue ?? null
    }
  },

  set: (key: string, value: any): void => {
    try {
      sessionStorage.setItem(`${PREFIX}${key}`, JSON.stringify(value))
    } catch (error) {
      console.error('Session storage set error:', error)
    }
  },

  remove: (key: string): void => {
    sessionStorage.removeItem(`${PREFIX}${key}`)
  },

  clear: (): void => {
    const keys = Object.keys(sessionStorage)
    keys.forEach((key) => {
      if (key.startsWith(PREFIX)) {
        sessionStorage.removeItem(key)
      }
    })
  },
}
