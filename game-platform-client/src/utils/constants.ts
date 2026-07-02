export const STORAGE_KEYS = {
  ACCESS_TOKEN: 'access_token',
  REFRESH_TOKEN: 'refresh_token',
  USER_INFO: 'user_info',
  THEME: 'theme',
  LANGUAGE: 'language',
  PREFERENCES: 'preferences',
} as const

export const GAME_CONFIG = {
  BOARD_SIZE: 15,
  WIN_COUNT: 5,
  DEFAULT_TIME_LIMIT: 300,
} as const

export const BREAKPOINTS = {
  xs: 475,
  sm: 640,
  md: 768,
  lg: 1024,
  xl: 1280,
  '2xl': 1536,
} as const

export const RECONNECT_INTERVAL = 3000
export const HEARTBEAT_INTERVAL = 30000
