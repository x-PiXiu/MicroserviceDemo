/**
 * WebSocket 连接管理
 *
 * 重构说明：
 * - 移除 Socket.io-client 依赖
 * - 使用 NativeWebSocket 封装原生 WebSocket
 * - 实现 authenticate() 和 setReady() 方法
 * - 保留现有 API 兼容性
 *
 * 设计参考：game_platform_client_optimization_plan.md 阶段1
 */
import { ref, computed, readonly } from 'vue'
import { ElMessage } from 'element-plus'
import {
  NativeWebSocket,
  createNativeWebSocket,
  type WebSocketStatus,
} from './nativesocket'
import type { ClientMessage, ServerMessage } from '@/types'

// v2.7.0: 懒加载 gameHandlers 以避免循环依赖
let gameHandlersModule: typeof import('./handlers/game.handler')['gameHandlers'] | null = null
const loadGameHandlers = async () => {
  if (!gameHandlersModule) {
    const module = await import('./handlers/game.handler')
    gameHandlersModule = module.gameHandlers
  }
  return gameHandlersModule
}

// WebSocket 基础 URL
// 如果端口是 8086（直连 WebSocket 服务器），不添加 /ws 路径
// 如果端口是 80（通过 Nginx 代理），添加 /ws 路径
import { getGomokuWebSocketUrl, isDirectMode } from '@/config/services'

// 获取 WebSocket URL（从配置模块获取）
const getWsBaseUrl = () => {
  const wsUrl = getGomokuWebSocketUrl()
  // 直连模式(8086) 不添加 /ws 路径，代理模式(80) 添加 /ws 路径
  return wsUrl.includes(':8086') ? wsUrl : wsUrl + '/ws'
}

const WS_BASE_URL = getWsBaseUrl()

// 调试日志
console.log('[WebSocket] Config - Mode:', isDirectMode ? 'direct' : 'proxied', 'URL:', WS_BASE_URL)

// 单例 WebSocket 实例
let wsInstance: NativeWebSocket | null = null

// 全局状态
const status = ref<WebSocketStatus>('disconnected')
const playerId = ref<string | null>(null)
const serverInfo = ref<{ name: string; version: string } | null>(null)

// 事件监听器映射
const eventListeners = new Map<string, Set<(data: any) => void>>()

/**
 * 通用 WebSocket 连接
 */
export function useWebSocket() {
  const isConnected = computed(() => status.value === 'connected' || status.value === 'authenticated')
  const isAuthenticated = computed(() => status.value === 'authenticated')

  /**
   * 连接 WebSocket
   */
  const connect = async (token?: string): Promise<NativeWebSocket | null> => {
    if (wsInstance?.isConnected) {
      return wsInstance
    }

    // 构建 WebSocket URL（带 token 参数）
    const url = token ? `${WS_BASE_URL}?token=${encodeURIComponent(token)}` : WS_BASE_URL

    // 创建 WebSocket 实例
    wsInstance = createNativeWebSocket({
      url,
      heartbeatInterval: 30000,
      maxReconnectAttempts: 5,
      reconnectInterval: 3000,
      debug: (import.meta as any).env?.DEV || false,
    })

    // 设置事件回调
    wsInstance.setEvents({
      onOpen: () => {
        console.log('[WebSocket] Connected')
      },
      onClose: (event) => {
        console.log('[WebSocket] Disconnected:', event.code, event.reason)
        // 清除所有事件监听器
        eventListeners.clear()
      },
      onError: (error) => {
        console.error('[WebSocket] Error:', error)
      },
      onMessage: (message) => {
        dispatchMessage(message)
      },
      onWelcome: (message) => {
        playerId.value = message.player_id
        serverInfo.value = message.server_info
      },
      onAuthSuccess: (message) => {
        console.log('[WebSocket] Authenticated:', message.data.playerId)
      },
      onStatusChange: (newStatus) => {
        status.value = newStatus
      },
    })

    // 同步状态引用
    if (wsInstance.playerId.value) {
      playerId.value = wsInstance.playerId.value
    }
    if (wsInstance.serverInfo.value) {
      serverInfo.value = wsInstance.serverInfo.value
    }

    try {
      await wsInstance.connect()
      return wsInstance
    } catch (error) {
      console.error('[WebSocket] Connection failed:', error)
      return null
    }
  }

  /**
   * 断开连接
   */
  const disconnect = () => {
    if (wsInstance) {
      wsInstance.disconnect()
      wsInstance = null
    }
    status.value = 'disconnected'
    playerId.value = null
    serverInfo.value = null
    eventListeners.clear()
  }

  /**
   * 发送消息
   */
  const send = (message: ClientMessage) => {
    if (wsInstance?.isConnected) {
      wsInstance.send(message)
    } else {
      ElMessage.warning('网络连接已断开')
    }
  }

  /**
   * 添加事件监听
   */
  const on = (event: string, callback: (data: any) => void) => {
    if (!eventListeners.has(event)) {
      eventListeners.set(event, new Set())
    }
    eventListeners.get(event)!.add(callback)
  }

  /**
   * 移除事件监听
   */
  const off = (event: string, callback?: (data: any) => void) => {
    if (callback) {
      eventListeners.get(event)?.delete(callback)
    } else {
      eventListeners.delete(event)
    }
  }

  /**
   * 分发消息到事件监听器
   * v2.7.0: 添加全局消息处理，确保关键消息（如 room_info）不会因组件切换而丢失
   */
  const dispatchMessage = (message: ServerMessage) => {
    // v2.7.0: 全局消息处理 - 自动调用 gameHandlers 处理关键消息
    // 这确保了即使组件没有注册监听器（如路由切换期间），消息也会被正确处理
    loadGameHandlers().then(handlers => {
      if (handlers) {
        try {
          handlers.handleMessage(message)
        } catch (error) {
          console.error('[WebSocket] Error in global message handler:', error)
        }
      }
    }).catch(err => {
      console.warn('[WebSocket] Failed to load gameHandlers:', err)
    })

    // 分发到注册的监听器
    const listeners = eventListeners.get(message.type)
    if (listeners) {
      listeners.forEach((callback) => {
        try {
          callback(message)
        } catch (error) {
          console.error(`[WebSocket] Error in listener for ${message.type}:`, error)
        }
      })
    }

    // 也分发到通用的 'message' 事件
    const allListeners = eventListeners.get('message')
    if (allListeners) {
      allListeners.forEach((callback) => {
        try {
          callback(message)
        } catch (error) {
          console.error('[WebSocket] Error in message listener:', error)
        }
      })
    }
  }

  return {
    // 状态
    status: readonly(status),
    isConnected,
    isAuthenticated,
    playerId: readonly(playerId),
    serverInfo: readonly(serverInfo),

    // 连接管理
    connect,
    disconnect,
    send,

    // 事件
    on,
    off,
  }
}

/**
 * 五子棋 WebSocket 连接
 * 专门用于五子棋游戏的 WebSocket 功能
 *
 * v2.1.0 更新：
 * - 支持在大厅页面建立连接（提前连接）
 * - 支持房间列表实时订阅
 * - 支持通过 WebSocket 创建/加入房间
 */
export function useGomokuSocket() {
  const {
    status: wsStatus,
    isConnected,
    isAuthenticated,
    playerId,
    connect,
    disconnect,
    send,
    on,
    off,
  } = useWebSocket()

  // 房间列表订阅状态
  const isRoomListSubscribed = ref(false)

  /**
   * 认证
   */
  const authenticate = (token: string, userId: string) => {
    if (wsInstance?.isConnected) {
      wsInstance.authenticate(token, userId)
    } else {
      // 如果未连接，先连接再认证
      connect(token).then(() => {
        wsInstance?.authenticate(token, userId)
      })
    }
  }

  /**
   * 设置准备状态
   */
  const setReady = (ready: boolean) => {
    if (wsInstance?.isAuthenticated) {
      wsInstance.setReady(ready)
    } else {
      ElMessage.warning('请先完成认证')
    }
  }

  // ===== v2.1.0 新增：房间列表订阅 =====

  /**
   * 订阅房间列表
   * @param gameType 游戏类型，默认 'gomoku'
   */
  const subscribeRoomList = (gameType: string = 'gomoku') => {
    if (wsInstance?.isAuthenticated) {
      send({ type: 'subscribe_room_list', data: { gameType } })
      console.log('[WebSocket] Subscribed to room list')
    } else {
      console.warn('[WebSocket] Cannot subscribe: not authenticated')
    }
  }

  /**
   * 取消订阅房间列表
   */
  const unsubscribeRoomList = () => {
    send({ type: 'unsubscribe_room_list', data: {} })
    isRoomListSubscribed.value = false
    console.log('[WebSocket] Unsubscribed from room list')
  }

  // ===== v2.1.0 新增：房间操作（WebSocket 版） =====

  /**
   * 通过 WebSocket 创建房间
   * @param creatorId 创建者 ID
   * @param config 房间配置
   */
  const createRoom = (creatorId: string, config: {
    gameMode: string
    timeLimit: number
    incrementPerMove: number
    allowUndo: boolean
    allowSpectators: boolean
    maxSpectators: number
  }) => {
    if (wsInstance?.isAuthenticated) {
      send({
        type: 'create_room',
        data: { creatorId, config }
      })
      console.log('[WebSocket] Creating room...')
    } else {
      ElMessage.warning('请先完成认证')
    }
  }

  /**
   * 通过 WebSocket 加入房间
   * @param roomId 房间 ID
   * @param role 角色：player 或 spectator
   */
  const joinRoomWS = (roomId: string, role: 'player' | 'spectator' = 'player') => {
    if (wsInstance?.isAuthenticated) {
      send({ type: 'join_room', data: { roomId, role } })
      console.log('[WebSocket] Joining room:', roomId)
    } else {
      ElMessage.warning('请先完成认证')
    }
  }

  /**
   * 进入房间页面（已加入房间后调用）
   * 通知服务器玩家进入了房间界面
   *
   * 注意：v2.7.0 服务端暂不支持 enter_room 消息
   * 此调用目前无效，但保留以备将来服务端支持
   * 匹配模式下 room_info 会自动推送，无需调用此方法
   *
   * @param roomId 房间 ID
   */
  const enterRoom = (roomId: string) => {
    if (wsInstance?.isAuthenticated) {
      send({ type: 'enter_room', data: { roomId } })
      console.log('[WebSocket] Entering room:', roomId)
    }
  }

  /**
   * 通过 WebSocket 开始游戏（房主调用）
   * v2.3.0: 使用 room_operation 消息格式
   * @param roomId 房间 ID（可选，如果在房间内可省略）
   */
  const startGame = (roomId?: string) => {
    if (wsInstance?.isAuthenticated) {
      send({
        type: 'room_operation',
        data: {
          operation: 'start_game',
          roomId: roomId || undefined
        }
      })
      console.log('[WebSocket] Starting game...', roomId)
    } else {
      ElMessage.warning('请先完成认证')
    }
  }

  /**
   * 加入房间（兼容旧 API，支持自动连接）
   * @deprecated 推荐使用 joinRoomWS（已连接状态下）或 connectAndJoin（自动连接）
   */
  const joinRoom = (roomId: string, token: string, userId: string) => {
    // 如果已经连接且认证，直接加入
    if (wsInstance?.isAuthenticated) {
      send({ type: 'join_room', data: { roomId } })
      return
    }

    // 否则先连接再认证再加入
    connect(token).then(() => {
      // 等待 welcome 消息后认证
      const handleWelcome = () => {
        authenticate(token, userId)
        off('welcome', handleWelcome)
      }
      on('welcome', handleWelcome)

      // 等待认证成功后加入房间
      const handleAuthSuccess = () => {
        send({ type: 'join_room', data: { roomId } })
        off('auth_success', handleAuthSuccess)
      }
      on('auth_success', handleAuthSuccess)
    })
  }

  /**
   * 离开房间
   */
  const leaveRoom = () => {
    send({ type: 'leave_room', data: {} })
  }

  /**
   * 落子
   */
  const makeMove = (position: { row: number; col: number }) => {
    send({ type: 'place_piece', data: { position } })
  }

  /**
   * 认输
   */
  const resign = () => {
    send({ type: 'surrender', data: {} })
  }

  /**
   * 求和
   */
  const requestDraw = () => {
    send({ type: 'draw_offer', data: {} })
  }

  /**
   * 响应求和
   */
  const respondDraw = (accepted: boolean) => {
    console.log('[WebSocket] Sending draw_response with accepted:', accepted)
    send({ type: 'draw_response', data: { accepted } })
  }

  /**
   * 悔棋请求
   */
  const requestUndo = () => {
    send({ type: 'undo_move', data: {} })
  }

  /**
   * 响应悔棋
   */
  const respondUndo = (accepted: boolean) => {
    send({ type: 'undo_response', data: { accepted } })
  }

  /**
   * 发送聊天消息
   */
  const sendChat = (message: string) => {
    send({ type: 'chat', data: { message } })
  }

  // ===== v2.6.0 新增：匹配系统 =====

  /**
   * 开始匹配
   * @param gameType 游戏类型，默认 'gomoku'
   * @param options 匹配选项
   */
  const startMatch = (gameType: string = 'gomoku', options?: {
    match_mode?: 'ranked' | 'casual'
    rating_range?: { min: number; max: number }
  }) => {
    if (wsInstance?.isAuthenticated) {
      send({
        type: 'match_start',
        data: {
          game_type: gameType,
          match_mode: options?.match_mode ?? 'ranked',
          rating_range: options?.rating_range,
        }
      })
      console.log('[WebSocket] Match started for:', gameType)
    } else {
      ElMessage.warning('请先完成认证')
    }
  }

  /**
   * 取消匹配
   */
  const cancelMatch = () => {
    if (wsInstance?.isAuthenticated) {
      send({ type: 'match_cancel', data: {} })
      console.log('[WebSocket] Match cancelled')
    } else {
      ElMessage.warning('请先完成认证')
    }
  }

  // 监听订阅确认
  on('subscription_confirm', (message: any) => {
    if (message.data.subscribed) {
      isRoomListSubscribed.value = true
    }
  })

  return {
    // 状态
    status: wsStatus,
    isConnected,
    isAuthenticated,
    playerId,
    isRoomListSubscribed: readonly(isRoomListSubscribed),

    // 连接管理
    connect,
    disconnect,

    // 认证与准备
    authenticate,
    setReady,

    // v2.1.0 新增：房间列表订阅
    subscribeRoomList,
    unsubscribeRoomList,

    // v2.1.0 新增：房间操作（WebSocket 版）
    createRoom,
    joinRoomWS,
    enterRoom,
    startGame,

    // 房间操作（兼容旧 API）
    joinRoom,
    leaveRoom,

    // 游戏操作
    makeMove,
    resign,
    requestDraw,
    respondDraw,
    requestUndo,
    respondUndo,
    sendChat,

    // v2.6.0 新增：匹配系统
    startMatch,
    cancelMatch,

    // 事件
    on,
    off,
    send,
  }
}

// 导出类型
export type { WebSocketStatus, NativeWebSocket }
