const LOG_LEVELS = {
  DEBUG: 'debug',
  INFO: 'info',
  WARN: 'warn',
  ERROR: 'error',
  OFF: 'off',
} as const

type LogLevel = typeof LOG_LEVELS[keyof typeof LOG_LEVELS]

const DEFAULT_LOG_LEVEL: LogLevel = (import.meta.env.VITE_LOG_LEVEL as LogLevel | undefined) ?? LOG_LEVELS.DEBUG

class Logger {
  private level: LogLevel
  private prefix: string

  constructor(prefix: string, level?: LogLevel) {
    this.prefix = prefix
    this.level = level || DEFAULT_LOG_LEVEL
  }

  private shouldLog(level: LogLevel): boolean {
    if (this.level === LOG_LEVELS.OFF) return false
    if (level === LOG_LEVELS.OFF) return false
    
    const levels = [LOG_LEVELS.DEBUG, LOG_LEVELS.INFO, LOG_LEVELS.WARN, LOG_LEVELS.ERROR]
    const currentIndex = levels.indexOf(this.level)
    const targetIndex = levels.indexOf(level)
    return currentIndex !== -1 && targetIndex <= currentIndex
  }

  debug(...args: any[]) {
    if (this.shouldLog(LOG_LEVELS.DEBUG)) {
      console.log(`[${this.prefix}] [DEBUG]`, ...args)
    }
  }

  info(...args: any[]) {
    if (this.shouldLog(LOG_LEVELS.INFO)) {
      console.log(`[${this.prefix}] [INFO]`, ...args)
    }
  }

  warn(...args: any[]) {
    if (this.shouldLog(LOG_LEVELS.WARN)) {
      console.warn(`[${this.prefix}] [WARN]`, ...args)
    }
  }

  error(...args: any[]) {
    if (this.shouldLog(LOG_LEVELS.ERROR)) {
      console.error(`[${this.prefix}] [ERROR]`, ...args)
    }
  }

  group(label: string) {
    if (this.shouldLog(LOG_LEVELS.DEBUG)) {
      console.group(`[${this.prefix}] ${label}`)
    }
  }

  groupEnd() {
    if (this.shouldLog(LOG_LEVELS.DEBUG)) {
      console.groupEnd()
    }
  }

  time(label: string) {
    if (this.shouldLog(LOG_LEVELS.DEBUG)) {
      console.time(`[${this.prefix}] ${label}`)
    }
  }

  timeEnd(label: string) {
    if (this.shouldLog(LOG_LEVELS.DEBUG)) {
      console.timeEnd(`[${this.prefix}] ${label}`)
    }
  }
}

export const createLogger = (prefix: string): Logger => {
  return new Logger(prefix)
}

export const logger = {
  auth: createLogger('Auth'),
  api: createLogger('API'),
  router: createLogger('Router'),
  component: createLogger('Component'),
  store: createLogger('Store'),
  ws: createLogger('WebSocket'),
  game: createLogger('Game'),
}

export default logger
