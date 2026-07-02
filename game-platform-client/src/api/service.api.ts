import { serviceRequest } from './request'
import type {
  ServiceInstance,
  ServiceQueryParams,
  ServiceListData,
  ServiceStatsApiResponse,
  ServiceRecommendApiResponse,
  HealthCheckResponse,
  GameService,
  ServiceListResponse,
  PlatformStats,
} from '@/types/service.types'
import { convertToGameService } from '@/types/service.types'

/**
 * 服务注册中心 API
 * 文档: Service_Registry_API_Documentation.md
 * 端口: 8090 (通过 nginx 转发)
 * 基础路径: /api/v1/services
 * 注意: 获取服务列表推荐使用 /api/v1/services/lists 避免 CORS 问题
 */
export const serviceApi = {
  // ==================== 服务发现 ====================

  /**
   * 获取服务列表
   * GET /api/v1/services/lists（推荐路径，避免 CORS 问题）
   * 支持多维度过滤
   * 注意：http 拦截器会自动提取 response.data，所以返回类型是 ServiceListData
   */
  getServices: (params?: ServiceQueryParams) =>
    serviceRequest.get<ServiceListData>('/api/v1/services/lists', { params }),

  /**
   * 获取所有游戏服务（仅游戏类型，排除核心服务）
   * 使用 service_type=game 过滤
   */
  getGameServices: (params?: {
    game_type?: string
    region?: string
    healthy_only?: boolean
    limit?: number
  }) =>
    serviceRequest.get<ServiceListData>('/api/v1/services/lists', {
      params: {
        service_type: 'game',
        healthy_only: true,
        ...params,
      },
    }),

  /**
   * 获取特定服务的所有实例
   * GET /api/v1/services/{service_name}
   */
  getServiceInstances: (serviceName: string) =>
    serviceRequest.get<{
      success: boolean
      message: string
      timestamp: number
      data: {
        service_name: string
        instances: ServiceInstance[]
        total_instances: number
        healthy_instances: number
      }
    }>(`/api/v1/services/${serviceName}`),

  /**
   * 获取特定实例详情
   * GET /api/v1/services/{service_name}/{host}:{port}
   */
  getServiceInstance: (serviceName: string, host: string, port: number) =>
    serviceRequest.get<{
      success: boolean
      message: string
      timestamp: number
      data: {
        service: ServiceInstance
      }
    }>(`/api/v1/services/${serviceName}/${host}:${port}`),

  /**
   * 获取服务健康状态
   * GET /api/v1/services/{service_name}/health
   */
  getServiceHealth: (serviceName: string) =>
    serviceRequest.get<{
      success: boolean
      message: string
      timestamp: number
      data: {
        service_name: string
        total_instances: number
        healthy_instances: number
        degraded_instances: number
        unhealthy_instances: number
        avg_health_score: number
        overall_status: 'healthy' | 'degraded' | 'unhealthy'
        instances: Array<{
          instance_id: string
          healthy: boolean
          health_score: number
          last_heartbeat_age: number
        }>
      }
    }>(`/api/v1/services/${serviceName}/health`),

  // ==================== 推荐服务 ====================

  /**
   * 获取推荐的服务实例（负载均衡）
   * GET /api/v1/services/recommend
   * 注意：HTTP 拦截器已提取 data，返回 ServiceRecommendApiResponse['data']
   */
  getRecommendedService: (params?: {
    service_type?: 'core' | 'game' | 'gateway'
    service_name?: string
    game_type?: string
    region?: string
    strategy?: 'least_load' | 'random' | 'round_robin' | 'weighted'
  }) =>
    serviceRequest.get<ServiceRecommendApiResponse['data']>('/api/v1/services/recommend', { params }),

  /**
   * 获取推荐的游戏服务器
   * 使用 service_type=game 过滤
   * 注意：HTTP 拦截器已提取 data，返回 { recommended, reason, candidates }
   */
  getRecommendedGameServer: (gameType?: string, strategy: 'least_load' | 'random' | 'round_robin' | 'weighted' = 'least_load') =>
    serviceRequest.get<ServiceRecommendApiResponse['data']>('/api/v1/services/recommend', {
      params: {
        service_type: 'game',
        game_type: gameType,
        strategy,
      },
    }),

  // ==================== 统计信息 ====================

  /**
   * 获取统计信息
   * GET /api/v1/services/stats
   */
  getStats: () =>
    serviceRequest.get<ServiceStatsApiResponse>('/api/v1/services/stats'),

  /**
   * 获取平台总体统计信息（转换为前端格式）
   * 优先从服务 metadata 获取 today_matches，回退到 stats API
   */
  getPlatformStats: async (): Promise<PlatformStats> => {
    try {
      // 先尝试从服务列表获取（metadata 包含 today_matches）
      const servicesResponse = await serviceApi.getGameServices({ healthy_only: true })
      const services = servicesResponse?.services || []

      // 从 metadata 汇总统计
      let totalTodayMatches = 0
      let totalOnlinePlayers = 0
      let totalActiveRooms = 0

      for (const service of services) {
        const meta = service.metadata
        totalOnlinePlayers += parseInt(meta.online_players || meta.current_players || '0', 10)
        totalActiveRooms += parseInt(meta.active_rooms || '0', 10)
        totalTodayMatches += parseInt(meta.today_matches || '0', 10)
      }

      console.log('[Service API] Platform stats from services metadata:', {
        totalOnlinePlayers,
        totalActiveRooms,
        totalTodayMatches
      })

      return {
        totalOnlinePlayers,
        totalActiveRooms,
        onlineGames: services.length,
        todayMatches: totalTodayMatches,
        platformUptime: 0,
      }
    } catch (error) {
      console.warn('[Service API] Failed to get stats from services, using fallback:', error)

      // 回退：从 stats API 获取
      try {
        const stats = await serviceRequest.get<ServiceStatsApiResponse>('/api/v1/services/stats')
        console.log('[Service API] Platform stats from stats API:', stats)
        return {
          totalOnlinePlayers: (stats as any)?.total_current_players || 0,
          totalActiveRooms: 0,
          onlineGames: Object.keys((stats as any)?.instances_by_game_type || {}).length,
          todayMatches: (stats as any)?.today_matches || 0,
          platformUptime: 0,
        }
      } catch {
        return {
          totalOnlinePlayers: 0,
          totalActiveRooms: 0,
          onlineGames: 0,
          todayMatches: 0,
          platformUptime: 0,
        }
      }
    }
  },

  // ==================== 健康检查 ====================

  /**
   * 服务注册中心健康检查
   * GET /api/v1/services/health
   */
  healthCheck: () =>
    serviceRequest.get<HealthCheckResponse>('/api/v1/services/health'),
}

/**
 * 游戏服务通用 API（动态路由）
 * 用于访问特定游戏服务的通用接口
 */
export const gameServiceApi = {
  /** 获取游戏服务状态 */
  getGameStatus: (gameType: string) =>
    serviceRequest.get<{
      online: boolean
      onlinePlayers: number
      activeRooms: number
      version: string
    }>(`/api/v1/${gameType}/status`),

  /** 获取游戏服务排行榜 */
  getGameLeaderboard: (gameType: string, params?: { limit?: number; mode?: string }) =>
    serviceRequest.get<{
      leaderboard: Array<{
        rank: number
        playerId: string
        playerName: string
        rating: number
        wins: number
        losses: number
      }>
    }>(`/api/v1/${gameType}/leaderboard`, { params }),
}

/**
 * 获取游戏服务列表（前端展示用）
 * 自动转换为前端友好的 GameService 格式
 */
export async function getGameServicesForDisplay(params?: {
  game_type?: string
  region?: string
  healthy_only?: boolean
}): Promise<ServiceListResponse> {
  try {
    // 注意：http 拦截器已经提取了 response.data，所以这里 response 就是 data 对象
    const response = await serviceApi.getGameServices(params)
    // response 已经是 { services: [...], count: number, timestamp: number }
    const services = response?.services || []

    const gameServices: GameService[] = services.map(convertToGameService)

    return {
      services: gameServices,
      totalCount: response?.count || gameServices.length,
      timestamp: response?.timestamp || Date.now(),
    }
  } catch {
    return {
      services: [],
      totalCount: 0,
      timestamp: Date.now(),
    }
  }
}
