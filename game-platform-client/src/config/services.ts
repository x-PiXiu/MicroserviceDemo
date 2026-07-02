/**
 * 服务配置 - 支持域名模式和直连模式
 *
 * 使用方式:
 * - VITE_USE_DIRECT_CONNECTION=true  -> 直连各服务 IP:端口
 * - VITE_USE_DIRECT_CONNECTION=false -> 通过 Nginx 代理（默认）
 *
 * 直连模式端口:
 * - service_registry: 8090
 * - auth_service: 8083
 * - user_service: 8082
 * - game_data_service: 8084
 * - gomoku_server: 8085 (HTTP API)
 * - gomoku_server: 8086 (WebSocket)
 */

export interface ServiceConfig {
  /** HTTP API 基础 URL */
  httpUrl: string
  /** WebSocket URL（仅游戏服务需要） */
  wsUrl?: string
  /** 服务名称（用于日志） */
  name: string
}

// ==================== 配置读取 ====================

const USE_DIRECT = import.meta.env.VITE_USE_DIRECT_CONNECTION === 'true'

// 域名模式下，所有服务通过同一个 Nginx 代理
// 直连模式下，配置各服务实际地址

const getServiceUrls = () => {
  if (USE_DIRECT) {
    // 直连模式
    const HOST = import.meta.env.VITE_SERVICE_HOST || '127.0.0.1'
    return {
      serviceRegistry: `http://${HOST}:8090`,
      authService: `http://${HOST}:8083`,
      userService: `http://${HOST}:8082`,
      gameDataService: `http://${HOST}:8084`,
      gomokuServer: `http://${HOST}:8085`,
      gomokuWebSocket: `ws://${HOST}:8086`,
    }
  } else {
    // 域名模式（通过 Nginx 代理）
    const API_BASE = import.meta.env.VITE_API_BASE_URL || 'http://43.143.124.220'
    const WS_BASE = import.meta.env.VITE_WS_BASE_URL || 'ws://43.143.124.220:80'
    return {
      serviceRegistry: API_BASE,
      authService: API_BASE,
      userService: API_BASE,
      gameDataService: API_BASE,
      gomokuServer: API_BASE,
      gomokuWebSocket: WS_BASE,
    }
  }
}

const urls = getServiceUrls()

// ==================== 服务配置 ====================

export const serviceConfig: Record<string, ServiceConfig> = {
  // 服务注册中心
  serviceRegistry: {
    name: 'service_registry',
    httpUrl: urls.serviceRegistry,  // http://HOST:8090
  },

  // 认证服务
  authService: {
    name: 'auth_service',
    httpUrl: `${urls.authService}/api/v1/auth`,
  },

  // 用户服务
  userService: {
    name: 'user_service',
    httpUrl: `${urls.userService}/api/v1/user`,
  },

  // 游戏数据服务
  gameDataService: {
    name: 'game_data_service',
    httpUrl: `${urls.gameDataService}/api/v1/gamedata`,
  },

  // 五子棋服务 (HTTP API)
  gomokuServer: {
    name: 'gomoku_server',
    httpUrl: `${urls.gomokuServer}/api/v1/gomoku`,
  },

  // 五子棋服务 (WebSocket)
  gomokuWebSocket: {
    name: 'gomoku_server',
    httpUrl: urls.gomokuWebSocket,
    wsUrl: urls.gomokuWebSocket,
  },
}

// ==================== 便捷访问函数 ====================

export const getAuthServiceUrl = () => serviceConfig.authService.httpUrl
export const getUserServiceUrl = () => serviceConfig.userService.httpUrl
export const getGameDataServiceUrl = () => serviceConfig.gameDataService.httpUrl
export const getGomokuServerUrl = () => serviceConfig.gomokuServer.httpUrl
export const getGomokuWebSocketUrl = () => serviceConfig.gomokuWebSocket.wsUrl || serviceConfig.gomokuWebSocket.httpUrl
export const getServiceRegistryUrl = () => serviceConfig.serviceRegistry.httpUrl

// ==================== 模式信息 ====================

export const connectionMode = USE_DIRECT ? 'direct' : 'proxied'
export const isDirectMode = USE_DIRECT

console.log(`[Service Config] Connection mode: ${connectionMode}`)
if (USE_DIRECT) {
  console.log('[Service Config] Direct connection to services enabled')
  console.log('[Service Config] Host:', import.meta.env.VITE_SERVICE_HOST || '127.0.0.1')
}
