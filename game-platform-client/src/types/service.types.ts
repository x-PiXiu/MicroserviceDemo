// ==================== 服务注册中心 API 响应类型 ====================
// 基于 Service_Registry_API_Documentation.md 和实际 API 响应

/** 健康分数类型（API 返回的是对象） */
export interface HealthScore {
  source: string
  parsedValue: number
}

/** 服务实例信息（来自 API 响应） */
export interface ServiceInstance {
  /** 服务ID */
  service_id: string
  /** 服务名称 */
  service_name: string
  /** 服务版本 */
  service_version: string
  /** 主机地址 */
  host: string
  /** 端口号 */
  port: number
  /** 实例ID (host:port) */
  instance_id: string
  /** 健康状态 */
  healthy: boolean
  /** 健康分数 (对象格式) */
  health_score: HealthScore
  /** 权重 (1-1000) */
  weight: number
  /** 最后心跳时间 (毫秒时间戳) */
  last_heartbeat: number
  /** 心跳延迟秒数 */
  heartbeat_age_seconds: number
  /** 注册时间 (毫秒时间戳) */
  register_time: number
  /** 运行时长秒数 */
  uptime_seconds: number
  /** 平均响应时间(毫秒) */
  avg_response_time_ms: number
  /** 连续失败次数 */
  consecutive_failures: number
  /** 连续成功次数 */
  consecutive_successes: number
  /** 总请求数 */
  total_requests: number
  /** 失败请求数 */
  failed_requests: number
  /** 健康检查端点 */
  health_check_endpoint: string
  /** API 端点列表 */
  endpoints: string[]
  /** 服务元数据 */
  metadata: {
    /** 服务类型 (core, game, gateway) */
    service_type?: 'core' | 'game' | 'gateway'
    /** 游戏类型 */
    game_type?: string
    /** 区域 */
    region?: string
    /** WebSocket 端口 */
    websocket_port?: string
    /** 当前玩家数（旧字段名，兼容） */
    current_players?: string
    /** 在线玩家数（新字段名） */
    online_players?: string
    /** 最大玩家数 */
    max_players?: string
    /** 活跃房间数 */
    active_rooms?: string
    /** 今日对局数 */
    today_matches?: string
    /** CPU 使用率 */
    cpu_usage?: string
    /** 内存使用率 */
    memory_usage?: string
    /** 服务器状态 */
    server_status?: string
    [key: string]: string | undefined
  }
}

/** 服务查询参数 */
export interface ServiceQueryParams {
  /** 按服务名称过滤 */
  service_name?: string
  /** 按服务类型过滤 (core, game, gateway) */
  service_type?: 'core' | 'game' | 'gateway'
  /** 按游戏类型过滤 */
  game_type?: string
  /** 按区域过滤 */
  region?: string
  /** 仅返回健康服务 */
  healthy_only?: boolean
  /** 最低健康分数 (0.0-1.0) */
  min_health_score?: number
  /** 最大当前玩家数 */
  max_current_players?: number
  /** 返回数量限制 */
  limit?: number
  /** 分页偏移量 */
  offset?: number
}

/** 分页信息 */
export interface PaginationInfo {
  total: number
  limit: number
  offset: number
  has_more: boolean
}

/** 服务列表响应（原始 API 响应） */
export interface ServiceListApiResponse {
  success: boolean
  message: string
  timestamp: number
  data: {
    services: ServiceInstance[]
    count: number
    timestamp?: number
  }
}

/** 服务列表数据（http 拦截器提取后的返回类型） */
export interface ServiceListData {
  services: ServiceInstance[]
  count: number
  timestamp?: number
}

/** 服务统计信息 */
export interface ServiceStats {
  /** 总服务数 */
  total_services: number
  /** 总实例数 */
  total_instances: number
  /** 健康实例数 */
  healthy_instances: number
  /** 不健康实例数 */
  unhealthy_instances: number
  /** 降级实例数 */
  degraded_instances: number
  /** 按服务名统计 */
  instances_by_service: Record<string, number>
  /** 按游戏类型统计 */
  instances_by_game_type: Record<string, number>
  /** 按区域统计 */
  instances_by_region: Record<string, number>
  /** 平均健康分数 */
  avg_health_score: number
  /** 平均 CPU 使用率 */
  avg_cpu_usage: number
  /** 平均内存使用率 */
  avg_memory_usage: number
  /** 总当前玩家数 */
  total_current_players: number
  /** 今日对局数 */
  today_matches?: number
  /** 更新时间 */
  updated_at: number
}

/** 统计信息响应 */
export interface ServiceStatsApiResponse {
  success: boolean
  message: string
  timestamp: number
  data: ServiceStats
}

/** 推荐实例信息 */
export interface RecommendedInstance {
  service_name: string
  host: string
  port: number
  health_score: number
  load_score: number
  metadata: ServiceInstance['metadata']
}

/** 推荐服务响应 */
export interface ServiceRecommendApiResponse {
  success: boolean
  message: string
  timestamp: number
  data: {
    recommended: RecommendedInstance
    reason: string
    candidates: Array<{
      instance_id: string
      load_score: number
    }>
  }
}

/** 健康检查响应 */
export interface HealthCheckResponse {
  status: 'healthy' | 'unhealthy'
  service: string
  version: string
  timestamp: number
  stats?: {
    total_services: number
    total_instances: number
    healthy_instances: number
  }
}

// ==================== 前端使用的游戏服务类型 ====================

/** 游戏服务信息（前端展示用） */
export interface GameService {
  /** 服务ID */
  serviceId: string
  /** 服务名称 */
  name: string
  /** 服务显示名称 */
  displayName: string
  /** 服务描述 */
  description: string
  /** 服务图标 */
  icon?: string
  /** 服务版本 */
  version: string
  /** HTTP 端口 */
  httpPort: number
  /** WebSocket 端口 */
  websocketPort?: number
  /** 主机地址 */
  host: string
  /** 服务状态 */
  status: 'online' | 'offline' | 'maintenance'
  /** 在线玩家数 */
  onlinePlayers: number
  /** 活跃房间数 */
  activeRooms: number
  /** 今日对局数 */
  todayMatches: number
  /** 游戏类型标识 */
  gameType: string
  /** 游戏标签 */
  tags: string[]
  /** 是否推荐 */
  featured: boolean
  /** 排序权重 */
  sortWeight: number
  /** 注册时间 */
  registeredAt: number
  /** 最后心跳时间 */
  lastHeartbeat: number
  /** 健康分数 */
  healthScore: number
}

/** 服务列表响应（前端用） */
export interface ServiceListResponse {
  services: GameService[]
  totalCount: number
  timestamp: number
}

/** 平台统计信息（前端用） */
export interface PlatformStats {
  /** 总在线玩家 */
  totalOnlinePlayers: number
  /** 总活跃房间 */
  totalActiveRooms: number
  /** 在线游戏数 */
  onlineGames: number
  /** 今日对局数 */
  todayMatches: number
  /** 平台运行时间 */
  platformUptime: number
}

/** 服务健康状态 */
export interface ServiceHealth {
  serviceId: string
  status: 'healthy' | 'unhealthy' | 'degraded'
  uptime: number
  lastCheck: number
  metrics?: {
    cpuUsage?: number
    memoryUsage?: number
    connectionCount?: number
    requestPerSecond?: number
  }
}

// ==================== 游戏分类 ====================

export const GameCategories = {
  BOARD: 'board',
  CARD: 'card',
  PUZZLE: 'puzzle',
  ACTION: 'action',
  SPORTS: 'sports',
} as const

export type GameCategory = (typeof GameCategories)[keyof typeof GameCategories]

// ==================== 预定义游戏信息 ====================

export interface GameTypeInfo {
  id: string
  name: string
  displayName: string
  description: string
  icon: string
  color: string
  gradient: string
  category: GameCategory
  tags: string[]
  rules?: string
  playerCount: {
    min: number
    max: number
  }
  estimatedDuration: string
  difficulty: 'easy' | 'medium' | 'hard'
}

/** 游戏类型配置 */
export const GameTypeConfigs: Record<string, GameTypeInfo> = {
  gomoku: {
    id: 'gomoku',
    name: 'gomoku',
    displayName: '五子棋',
    description: '经典五子棋对弈，先连成五子者获胜',
    icon: '♟️',
    color: '#667eea',
    gradient: 'linear-gradient(135deg, #667eea 0%, #764ba2 100%)',
    category: GameCategories.BOARD,
    tags: ['策略', '双人对战', '经典'],
    playerCount: { min: 2, max: 2 },
    estimatedDuration: '10-30分钟',
    difficulty: 'medium',
  },
  chess: {
    id: 'chess',
    name: 'chess',
    displayName: '国际象棋',
    description: '世界上最受欢迎的策略棋类游戏',
    icon: '♞',
    color: '#f59e0b',
    gradient: 'linear-gradient(135deg, #f59e0b 0%, #d97706 100%)',
    category: GameCategories.BOARD,
    tags: ['策略', '双人对战', '经典'],
    playerCount: { min: 2, max: 2 },
    estimatedDuration: '15-60分钟',
    difficulty: 'hard',
  },
  reversi: {
    id: 'reversi',
    name: 'reversi',
    displayName: '黑白棋',
    description: '翻转棋子，占据更多地盘',
    icon: '⚫',
    color: '#10b981',
    gradient: 'linear-gradient(135deg, #10b981 0%, #059669 100%)',
    category: GameCategories.BOARD,
    tags: ['策略', '双人对战'],
    playerCount: { min: 2, max: 2 },
    estimatedDuration: '10-20分钟',
    difficulty: 'easy',
  },
  tictactoe: {
    id: 'tictactoe',
    name: 'tictactoe',
    displayName: '井字棋',
    description: '简单有趣的快速对战游戏',
    icon: '×',
    color: '#ec4899',
    gradient: 'linear-gradient(135deg, #ec4899 0%, #be185d 100%)',
    category: GameCategories.BOARD,
    tags: ['休闲', '快速对战'],
    playerCount: { min: 2, max: 2 },
    estimatedDuration: '1-5分钟',
    difficulty: 'easy',
  },
}

/** 获取游戏类型信息 */
export function getGameTypeInfo(gameType: string): GameTypeInfo | undefined {
  return GameTypeConfigs[gameType]
}

/** 获取所有游戏类型 */
export function getAllGameTypes(): GameTypeInfo[] {
  return Object.values(GameTypeConfigs)
}

/** 将 API 返回的 ServiceInstance 转换为前端使用的 GameService */
export function convertToGameService(instance: ServiceInstance): GameService {
  const gameType = instance.metadata.game_type || 'unknown'
  const typeInfo = getGameTypeInfo(gameType)

  // 处理 health_score，可能是对象或数字
  const healthScoreValue = typeof instance.health_score === 'object'
    ? instance.health_score.parsedValue
    : instance.health_score

  return {
    serviceId: instance.service_id || instance.instance_id,
    name: instance.service_name,
    displayName: typeInfo?.displayName || instance.service_name,
    description: typeInfo?.description || `${instance.service_name} 游戏服务`,
    icon: typeInfo?.icon,
    version: instance.service_version,
    httpPort: instance.port,
    websocketPort: instance.metadata.websocket_port ? parseInt(instance.metadata.websocket_port, 10) : undefined,
    host: instance.host,
    status: instance.healthy ? 'online' : 'offline',
    // 优先使用 online_players（新字段），兼容 current_players（旧字段）
    onlinePlayers: parseInt(instance.metadata.online_players || instance.metadata.current_players || '0', 10),
    activeRooms: parseInt(instance.metadata.active_rooms || '0', 10),
    todayMatches: parseInt(instance.metadata.today_matches || '0', 10),
    gameType,
    tags: typeInfo?.tags || [],
    featured: gameType === 'gomoku', // 默认五子棋为推荐
    sortWeight: Math.round(healthScoreValue * 100),
    registeredAt: instance.register_time,
    lastHeartbeat: instance.last_heartbeat,
    healthScore: healthScoreValue,
  }
}
