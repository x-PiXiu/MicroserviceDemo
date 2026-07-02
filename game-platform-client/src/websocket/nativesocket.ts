/**
 * NativeWebSocket - 原生 WebSocket 封装类
 *
 * 功能：
 * - 连接管理 (connect, disconnect)
 * - 消息队列 (发送缓冲)
 * - 心跳机制 (30秒间隔)
 * - 自动重连 (最多5次，间隔3秒)
 * - 状态管理 (connecting/connected/authenticated/disconnected/error)
 *
 * 设计参考：game_platform_client_optimization_plan.md 阶段1
 */
import { ref, type Ref } from 'vue'
import { ElMessage } from 'element-plus'
import type { ServerMessage, ClientMessage, WelcomeMessage, AuthSuccessMessage } from '@/types'

export type WebSocketStatus = 'disconnected' | 'connecting' | 'connected' | 'authenticated' | 'error'

export interface NativeWebSocketOptions {
  /** WebSocket 服务器地址 */
  url: string
  /** 心跳间隔（毫秒），默认 30000 */
  heartbeatInterval?: number
  /** 最大重连次数，默认 5 */
  maxReconnectAttempts?: number
  /** 重连间隔（毫秒），默认 3000 */
  reconnectInterval?: number
  /** 连接超时（毫秒），默认 10000 */
  connectionTimeout?: number
  /** 是否开启调试日志 */
  debug?: boolean
}

export interface NativeWebSocketEvents {
  onOpen?: () => void
  onClose?: (event: CloseEvent) => void
  onError?: (error: Event) => void
  onMessage?: (message: ServerMessage) => void
  onWelcome?: (message: WelcomeMessage) => void
  onAuthSuccess?: (message: AuthSuccessMessage) => void
  onStatusChange?: (status: WebSocketStatus) => void
}

/**
 * 原生 WebSocket 封装类
 */
export class NativeWebSocket {
  private ws: WebSocket | null = null
  private options: Required<NativeWebSocketOptions>
  private events: NativeWebSocketEvents = {}

  // 状态
  public status: Ref<WebSocketStatus> = ref('disconnected')
  public playerId: Ref<string | null> = ref(null)
  public serverInfo: Ref<{ name: string; version: string } | null> = ref(null)

  // 认证相关（保留用于重连后自动认证）
  // @ts-expect-error 保留用于未来重连认证功能
  private _pendingToken: string | null = null
  // @ts-expect-error 保留用于未来重连认证功能
  private _pendingPlayerId: string | null = null

  // 消息队列
  private messageQueue: ClientMessage[] = []

  // 心跳
  private heartbeatTimer: ReturnType<typeof setInterval> | null = null

  // 重连
  private reconnectAttempts = 0
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null

  // 连接超时
  private connectionTimer: ReturnType<typeof setTimeout> | null = null

  constructor(options: NativeWebSocketOptions) {
    this.options = {
      heartbeatInterval: 30000,
      maxReconnectAttempts: 5,
      reconnectInterval: 3000,
      connectionTimeout: 10000,
      debug: false,
      ...options,
    }
  }

  /**
   * 设置事件回调
   */
  public setEvents(events: NativeWebSocketEvents): void {
    this.events = { ...this.events, ...events }
  }

  /**
   * 获取连接状态
   */
  public get isConnected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN
  }

  public get isAuthenticated(): boolean {
    return this.status.value === 'authenticated'
  }

  /**
   * 连接 WebSocket
   */
  public async connect(): Promise<void> {
    if (this.ws?.readyState === WebSocket.OPEN) {
      this.log('Already connected')
      return
    }

    return new Promise((resolve, reject) => {
      try {
        this.setStatus('connecting')
        this.log('Connecting to', this.options.url)

        // 创建 WebSocket 连接
        this.ws = new WebSocket(this.options.url)

        // 设置连接超时
        this.connectionTimer = setTimeout(() => {
          if (this.status.value === 'connecting') {
            this.log('Connection timeout')
            this.handleConnectionError(new Event('timeout'))
            reject(new Error('Connection timeout'))
          }
        }, this.options.connectionTimeout)

        // 连接打开
        this.ws.onopen = () => {
          this.clearConnectionTimer()
          this.reconnectAttempts = 0
          this.setStatus('connected')
          this.log('Connected')

          // 触发事件
          this.events.onOpen?.()

          // 开始心跳
          this.startHeartbeat()

          // 刷新消息队列
          this.flushMessageQueue()

          resolve()
        }

        // 接收消息
        this.ws.onmessage = (event) => {
          this.handleMessage(event)
        }

        // 连接关闭
        this.ws.onclose = (event) => {
          this.clearConnectionTimer()
          this.handleClose(event)
          resolve() // Resolve anyway to not block
        }

        // 连接错误
        this.ws.onerror = (error) => {
          this.clearConnectionTimer()
          this.handleConnectionError(error)
          reject(error)
        }
      } catch (error) {
        this.setStatus('error')
        reject(error)
      }
    })
  }

  /**
   * 断开连接
   */
  public disconnect(): void {
    this.log('Disconnecting...')

    // 停止心跳
    this.stopHeartbeat()

    // 清除重连定时器
    this.clearReconnectTimer()

    // 清除连接超时
    this.clearConnectionTimer()

    // 清空消息队列
    this.messageQueue = []

    // 关闭 WebSocket
    if (this.ws) {
      this.ws.onopen = null
      this.ws.onmessage = null
      this.ws.onclose = null
      this.ws.onerror = null

      if (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING) {
        this.ws.close(1000, 'Client disconnect')
      }
      this.ws = null
    }

    this.setStatus('disconnected')
    this._pendingToken = null
    this._pendingPlayerId = null
  }

  /**
   * 发送消息
   */
  public send(message: ClientMessage): void {
    if (this.isConnected) {
      const json = JSON.stringify(message)
      this.ws!.send(json)
      this.log('Sent:', message.type, message)
    } else {
      // 加入队列等待连接后发送
      this.log('Queued:', message.type)
      this.messageQueue.push(message)
    }
  }

  /**
   * 认证
   */
  public authenticate(token: string, playerId: string): void {
    this._pendingToken = token
    this._pendingPlayerId = playerId

    // 发送认证消息
    this.send({
      type: 'authenticate',
      data: { token, playerId },
    })
  }

  /**
   * 发送准备状态
   */
  public setReady(ready: boolean): void {
    this.send({
      type: 'ready',
      data: { ready },
    })
  }

  /**
   * 处理收到的消息
   */
  private handleMessage(event: MessageEvent): void {
    try {
      const message: ServerMessage = JSON.parse(event.data)
      this.log('Received:', message.type, message)

      // 处理特殊消息类型
      switch (message.type) {
        case 'welcome':
          this.handleWelcome(message as WelcomeMessage)
          break
        case 'auth_success':
          this.handleAuthSuccess(message as AuthSuccessMessage)
          break
        case 'error':
          this.handleError(message as any)
          break
        default:
          // 触发通用消息事件
          this.events.onMessage?.(message)
      }
    } catch (error) {
      this.log('Failed to parse message:', error)
    }
  }

  /**
   * 处理 welcome 消息
   */
  private handleWelcome(message: WelcomeMessage): void {
    this.playerId.value = message.player_id
    this.serverInfo.value = message.server_info
    this.log('Welcome received, player_id:', message.player_id)

    // 触发事件
    this.events.onWelcome?.(message)
    this.events.onMessage?.(message)
  }

  /**
   * 处理认证成功消息
   */
  private handleAuthSuccess(message: AuthSuccessMessage): void {
    this.setStatus('authenticated')
    this.log('Authentication successful')

    // 清除待处理的认证信息
    this._pendingToken = null
    this._pendingPlayerId = null

    // 触发事件
    this.events.onAuthSuccess?.(message)
    this.events.onMessage?.(message)
  }

  /**
   * 处理错误消息
   */
  private handleError(message: { error_code: string; error_message: string }): void {
    this.log('Error:', message.error_code, message.error_message)
    ElMessage.error(message.error_message)
  }

  /**
   * 处理连接关闭
   */
  private handleClose(event: CloseEvent): void {
    this.log('Connection closed:', event.code, event.reason)

    this.stopHeartbeat()
    this.setStatus('disconnected')

    // 触发事件
    this.events.onClose?.(event)

    // 尝试重连（非正常关闭且非主动断开）
    if (event.code !== 1000 && this.reconnectAttempts < this.options.maxReconnectAttempts) {
      this.attemptReconnect()
    }
  }

  /**
   * 处理连接错误
   */
  private handleConnectionError(error: Event): void {
    this.log('Connection error:', error)
    this.setStatus('error')

    // 触发事件
    this.events.onError?.(error)

    // 尝试重连
    if (this.reconnectAttempts < this.options.maxReconnectAttempts) {
      this.attemptReconnect()
    } else {
      ElMessage.error('连接失败，请刷新页面重试')
    }
  }

  /**
   * 尝试重连
   */
  private attemptReconnect(): void {
    this.reconnectAttempts++
    this.log(`Attempting reconnect ${this.reconnectAttempts}/${this.options.maxReconnectAttempts}`)

    this.reconnectTimer = setTimeout(() => {
      this.connect().catch((error) => {
        this.log('Reconnect failed:', error)
      })
    }, this.options.reconnectInterval)
  }

  /**
   * 开始心跳
   */
  private startHeartbeat(): void {
    this.stopHeartbeat()

    this.heartbeatTimer = setInterval(() => {
      if (this.isConnected) {
        this.send({ type: 'heartbeat', data: {} })
      }
    }, this.options.heartbeatInterval)
  }

  /**
   * 停止心跳
   */
  private stopHeartbeat(): void {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer)
      this.heartbeatTimer = null
    }
  }

  /**
   * 刷新消息队列
   */
  private flushMessageQueue(): void {
    while (this.messageQueue.length > 0 && this.isConnected) {
      const message = this.messageQueue.shift()!
      this.send(message)
    }
  }

  /**
   * 设置状态
   */
  private setStatus(status: WebSocketStatus): void {
    this.status.value = status
    this.events.onStatusChange?.(status)
  }

  /**
   * 清除重连定时器
   */
  private clearReconnectTimer(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
    this.reconnectAttempts = 0
  }

  /**
   * 清除连接超时定时器
   */
  private clearConnectionTimer(): void {
    if (this.connectionTimer) {
      clearTimeout(this.connectionTimer)
      this.connectionTimer = null
    }
  }

  /**
   * 调试日志
   */
  private log(...args: any[]): void {
    if (this.options.debug) {
      console.log('[NativeWebSocket]', ...args)
    }
  }
}

/**
 * 创建 NativeWebSocket 实例
 */
export function createNativeWebSocket(options: NativeWebSocketOptions): NativeWebSocket {
  return new NativeWebSocket(options)
}
