/**
 * Game Data Service 类型定义
 *
 * 文档依据: game_platform_gameplay_design.md
 * - §8.1.1 user_profile 表结构
 * - §9.1.1 GET /profiles/{user_id} 返回格式
 * - §14.1.1 EloRatingCalculator 段位枚举
 * - §14.1.2 CurrencyManager 货币类型
 */

// ==================== 通用响应格式 ====================

/**
 * API 标准响应包装
 */
export interface GameDataApiResponse<T> {
  success: boolean
  message: string
  service: string
  timestamp: number
  data?: T
}

// ==================== 游戏档案相关 ====================

/**
 * 用户游戏档案 (API 返回格式)
 *
 * 文档依据: §8.1.1 user_profile 表结构
 * 包含: 基本信息、评分系统、段位信息、统计数据
 */
export interface GameProfile {
  // ========== 基本信息 ==========
  user_id: string
  display_name: string
  level: number
  experience_points: number
  avatar_url?: string
  created_at: number
  last_login_at: number

  // ========== 评分系统 (文档 §8.1.1) ==========
  /** 当前 ELO 评分 */
  current_rating: number
  /** 最高 ELO 评分 */
  peak_rating: number

  // ========== 段位信息 (文档 §4.2.2) ==========
  /** 段位枚举值 (0-15, 对应青铜III到大师) */
  tier: number
  /** 段位等级 (0-15, 与 tier 相同，文档 §10.2.2) */
  tier_level: number
  /** 段位名称，如 "黄金 II" */
  tier_name: string

  // ========== 统计数据 (文档 §8.1.1) ==========
  /** 总游戏场次 */
  total_games: number
  /** 胜场 */
  wins: number
  /** 负场 */
  losses: number
  /** 平局 */
  draws: number
  /** 当前连胜 */
  current_win_streak: number
  /** 最佳连胜 */
  best_win_streak: number
  /** 总游戏时长（秒） */
  total_playtime_seconds: number

  // ========== 计算字段（服务端返回） ==========
  /**
   * 胜率 (0.0-1.0)
   * 服务端计算返回，客户端显示时需乘以 100 转为百分比
   */
  win_rate: number
}

/**
 * 扩展的用户游戏档案（包含完整统计）
 * 用于 ProfileView 页面展示
 */
export interface ExtendedGameProfile extends GameProfile {
  // ========== 额外计算字段 ==========
  /** 平均每局时长（秒） */
  average_game_duration?: number
  /** 到下一段位需要的评分 */
  rating_to_next_tier?: number
  /** 段位内进度 (0-100) */
  tier_progress?: number
}

/**
 * 游戏结果类型
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md §3.4
 * 注意：使用大写形式，与服务端保持一致
 */
export type GameResult = 'WIN' | 'LOSS' | 'DRAW' | 'SURRENDER' | 'TIMEOUT' | 'DISCONNECT'

/**
 * 游戏结果类型（小写版本，用于向后兼容）
 * @deprecated 请使用 GameResult（大写形式）
 */
export type GameResultLowercase = 'win' | 'lose' | 'draw'

/**
 * 段位枚举字符串
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md §3.1
 */
export type TierString =
  | 'BRONZE_III' | 'BRONZE_II' | 'BRONZE_I'
  | 'SILVER_III' | 'SILVER_II' | 'SILVER_I'
  | 'GOLD_III' | 'GOLD_II' | 'GOLD_I'
  | 'PLATINUM_III' | 'PLATINUM_II' | 'PLATINUM_I'
  | 'DIAMOND_III' | 'DIAMOND_II' | 'DIAMOND_I'
  | 'MASTER'

/**
 * 结算数据结构
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md §3.4
 */
export interface GameSettlementData {
  user_id: string
  /** 游戏结果（大写形式） */
  result: GameResult

  // 评分变化（扁平结构，与文档一致）
  rating_before: number
  rating_after: number
  rating_change: number

  // 段位变化（使用字符串枚举）
  tier_before: TierString
  tier_after: TierString
  tier_changed: boolean
  tier_promoted: boolean
  tier_demoted: boolean

  // 奖励
  reward: {
    experience: number
    currency_rewards: readonly {
      type: CurrencyType
      amount: number
    }[]
  }

  // 更新后的统计数据
  new_win_streak: number
  new_best_win_streak?: number
  new_games_played?: number
  new_games_today?: number
  new_wins?: number
  new_losses?: number
  new_draws?: number
  new_level?: number

  // 成就（支持字符串数组或对象数组）
  achievements_unlocked: readonly (string | {
    id: string
    name: string
    rarity: 'COMMON' | 'RARE' | 'EPIC' | 'LEGENDARY'
  })[]

  // 排行榜变化
  leaderboard_rank_change?: {
    old_rank: number
    new_rank: number
  }

  // ===== 向后兼容字段 =====

  /** @deprecated 使用 rating_before 替代 */
  rating?: {
    before: number
    after: number
    change: number
  }

  /** @deprecated 使用 tier_before/tier_after 替代 */
  tier?: {
    before: number
    after: number
    name_before: string
    name_after: string
  }

  /** @deprecated 使用 reward 替代 */
  rewards?: {
    gold: number
    experience: number
    honor: number
    honor_points?: number  // 向后兼容
    gem?: number
    gems?: number          // 向后兼容
  }

  /** @deprecated 使用 achievements_unlocked 替代 */
  achievements_unlocked_detailed?: readonly {
    id: string
    name: string
    rarity: 'COMMON' | 'RARE' | 'EPIC' | 'LEGENDARY'
  }[]
}

/**
 * 创建用户游戏档案请求
 */
export interface CreateGameProfileRequest {
  user_id: string
  display_name: string
  level?: number
  experience_points?: number
  avatar_url?: string
}

/**
 * 更新用户游戏档案请求
 */
export interface UpdateGameProfileRequest {
  display_name?: string
  level?: number
  experience_points?: number
  avatar_url?: string
}

/**
 * 用户档案列表响应
 */
export interface GameProfileListData {
  items: GameProfile[]
  total_count: number
  page: number
  limit: number
  total_pages: number
}

// ==================== 货币相关 ====================

/**
 * 货币类型
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md §2.1
 * - gold: 金币 - 基础货币
 * - gem: 宝石 - 高级货币（注意：单数形式）
 * - honor: 荣誉值 - 竞技货币
 */
export type CurrencyType = 'gold' | 'gem' | 'honor'

/**
 * 货币显示名称映射
 */
export const CURRENCY_NAMES: Record<CurrencyType, string> = {
  gold: '金币',
  gem: '宝石',
  honor: '荣誉值'
}

/**
 * 货币图标映射
 */
export const CURRENCY_ICONS: Record<CurrencyType, string> = {
  gold: '🪙',
  gem: '💎',
  honor: '🏅'
}

/**
 * 旧货币类型到新类型的映射
 * 用于向后兼容
 */
export const CURRENCY_TYPE_MAP: Record<string, CurrencyType> = {
  coins: 'gold',
  gems: 'gem',          // 复数映射到单数
  tokens: 'honor',
  honor_points: 'honor',
  gold: 'gold',
  gem: 'gem',
  honor: 'honor',
  // 大写映射（服务端返回大写）
  GOLD: 'gold',
  GEM: 'gem',
  HONOR: 'honor',
  COINS: 'gold',
  GEMS: 'gem',
  TOKENS: 'honor',
  HONOR_POINTS: 'honor'
}

/**
 * 标准化货币类型
 */
export function normalizeCurrencyType(type: string): CurrencyType {
  return CURRENCY_TYPE_MAP[type] || 'gold'
}

/**
 * 单个货币信息
 */
export interface Currency {
  currency_type: CurrencyType
  amount: number
  last_updated_at: number
}

/**
 * 获取用户所有货币响应
 */
export interface CurrencyListData {
  user_id: string
  currencies: Currency[]
  total_currencies: number
}

/**
 * 获取用户特定货币响应
 */
export interface CurrencyData {
  user_id: string
  currency_type: string
  amount: number
  last_updated_at: number
}

/**
 * 更新用户货币请求
 */
export interface UpdateCurrencyRequest {
  user_id: string
  currency_type: CurrencyType
  amount: number
  operation: 'set' | 'add' | 'subtract'
}

/**
 * 更新用户货币响应
 */
export interface UpdateCurrencyData {
  user_id: string
  currency_type: string
  previous_amount: number
  new_amount: number
  change_amount: number
  last_updated_at: number
}

// ==================== 成就相关 ====================

/**
 * 成就信息 (API 返回格式)
 */
/**
 * 用户成就（服务端返回格式）
 *
 * 注意：服务端返回的字段名与文档略有不同
 * - title 而非 name
 * - 没有 user_id 和 unlocked_at 字段
 */
export interface GameDataAchievement {
  achievement_id: string
  achievement_type: string
  title: string
  description: string
  points: number
}

/**
 * 解锁成就请求
 */
export interface UnlockAchievementRequest {
  user_id: string
  achievement_type: string
  title: string
  description: string
  points: number
}

/**
 * 用户成就列表响应
 * 服务端实际返回格式（不包含 user_id）
 */
export interface GameDataAchievementListData {
  items: GameDataAchievement[]
  limit: number
  page: number
  total_count: number
  total_pages: number
}

// ==================== 库存相关 ====================

/**
 * 库存物品类型
 */
export type ItemType = 'powerup' | 'skin' | 'consumable' | 'equipment'

/**
 * 库存物品
 */
export interface InventoryItem {
  item_id: string
  user_id: string
  item_type: ItemType
  item_name: string
  quantity: number
  metadata?: Record<string, string>
  acquired_at: number
}

/**
 * 添加库存物品请求
 */
export interface AddInventoryItemRequest {
  user_id: string
  item_type: ItemType
  item_name: string
  quantity: number
  metadata?: Record<string, string>
}

/**
 * 使用库存物品请求
 */
export interface UseInventoryItemRequest {
  item_id: string
  quantity: number
}

/**
 * 使用库存物品响应
 */
export interface UseInventoryItemData {
  item_id: string
  user_id: string
  quantity_used: number
  remaining_quantity: number
  effect_applied: boolean
}

/**
 * 用户库存列表响应
 */
export interface InventoryListData {
  user_id: string
  total_items: number
  items: InventoryItem[]
}

// ==================== 排行榜相关 ====================

/**
 * 排行榜类型（按维度分类）
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md §4.1
 */
export type LeaderboardType =
  | 'rating'        // 评分排行榜
  | 'win_streak'    // 连胜排行榜
  | 'playtime'      // 游戏时长排行榜
  | 'win_rate'      // 胜率排行榜
  | 'experience'    // 经验排行榜
  | 'achievements'  // 成就点数排行榜

/**
 * 排行榜范围（按时间分类）
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md §4.2
 */
export type LeaderboardScope =
  | 'global'    // 全球榜
  | 'weekly'    // 周榜
  | 'monthly'   // 月榜
  | 'seasonal'  // 赛季榜

/**
 * 排行榜类型（旧版本，已废弃，请使用 LeaderboardType + LeaderboardScope）
 * @deprecated 使用 LeaderboardType 和 LeaderboardScope 替代
 */
export type GameDataLeaderboardType = 'global' | 'weekly' | 'monthly'

/**
 * 游戏类型 (Game Data Service)
 */
export type GameDataGameType = 'gomoku' | 'snake'

/**
 * 排行榜条目
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md §4.3.1
 */
export interface LeaderboardEntry {
  /** 排名位置 */
  rank: number
  /** 用户ID */
  user_id: string
  /** 显示名称 */
  display_name: string
  /** 评分（根据排行榜类型，可能是 ELO 评分、连胜数、胜率等） */
  score: number
  /** 段位名称（英文，如 "Gold II"） */
  tier_name: string
  /** 胜率（0.0-1.0，如 0.785 表示 78.5%） */
  win_rate: number
  /** 游戏场次 */
  games_played: number
  /** 头像 URL */
  avatar_url: string
  /** 段位枚举值（可选，用于显示段位图标） */
  tier?: number
}

/**
 * 排行榜条目（旧格式，用于兼容）
 * @deprecated 请使用 LeaderboardEntry 替代
 */
export interface GameDataLeaderboardEntry {
  rank_position: number
  user_id: string
  display_name?: string
  score: number
  tier?: number
  tier_name?: string
  extra_data?: {
    wins?: number
    losses?: number
    draws?: number
    win_rate?: number
    level?: number
  }
  recorded_at: number
}

/**
 * 排行榜完整数据
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md §4.3.1
 */
export interface LeaderboardData {
  /** 排行榜类型 */
  type: LeaderboardType
  /** 排行榜范围 */
  scope: LeaderboardScope
  /** 更新时间 */
  updated_at: string
  /** 总玩家数 */
  total_players: number
  /** 排行榜条目 */
  entries: LeaderboardEntry[]
  /** 当前用户排名（可选，需要传入 user_id 参数） */
  my_rank?: LeaderboardEntry
}

/**
 * 排行榜数据（旧格式）
 * @deprecated 请使用 LeaderboardData 替代
 */
export interface GameDataLeaderboardData {
  leaderboard_type: string
  game_type: string
  total_entries: number
  last_updated: number
  entries: GameDataLeaderboardEntry[]
}

/**
 * 更新排行榜请求
 */
export interface UpdateLeaderboardRequest {
  user_id: string
  leaderboard_type: LeaderboardType
  game_type: GameDataGameType
  score: number
  extra_data?: {
    wins?: number
    losses?: number
    win_rate?: number
  }
}

/**
 * 更新排行榜响应
 */
export interface UpdateLeaderboardData {
  entry_id: string
  user_id: string
  leaderboard_type: string
  game_type: string
  score: number
  rank_position: number
  extra_data?: Record<string, number>
  recorded_at: number
}

/**
 * 用户排名信息
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md §4.3.2
 */
export interface UserRankData {
  user_id: string
  leaderboard_type: LeaderboardType
  game_type: GameDataGameType
  scope: LeaderboardScope
  rank: number
  on_leaderboard: boolean
  display_name: string
  score: number
  tier_name: string
  win_rate: number
  games_played: number
  avatar_url: string
}

/**
 * 用户排名信息（旧格式）
 * @deprecated 请使用 UserRankData 替代
 */
export interface GameDataUserRankData {
  user_id: string
  leaderboard_type: string
  game_type: string
  rank_position: number
  score: number
  total_players: number
  percentile: number
  extra_data?: {
    wins?: number
    losses?: number
    win_rate?: number
  }
  recorded_at: number
}

// ==================== 每日统计相关 ====================

/**
 * 每日统计数据
 *
 * 文档依据: client_economy_rating_leaderboard_integration_guide.md §10.2.2
 */
export interface DailyStats {
  user_id: string
  game_type_id: number
  /** 统计日期 YYYY-MM-DD */
  stat_date: string
  /** 今日游戏场次 */
  games_played: number
  /** 今日胜场 */
  games_won: number
  /** 今日负场 */
  games_lost: number
  /** 今日平局 */
  games_drawn: number
  /** 是否已获得首胜奖励 */
  first_win_claimed: boolean
  /** 今日总游戏时长（秒） */
  total_playtime_seconds: number
  /** 今日获得金币 */
  total_gold_earned: number
  /** 今日获得荣誉 */
  total_honor_earned: number
  /** 今日获得经验 */
  total_exp_earned: number
  /** 今日评分变化 */
  rating_change: number
  /** 今日最高连胜 */
  peak_streak: number
}

// ==================== 系统相关 ====================

/**
 * 健康检查响应
 */
export interface HealthCheckData {
  status: string
  service: string
  version: string
  timestamp: number
  uptime_seconds: number
  components: {
    mysql: string
    redis: string
    http_server: string
    timer_manager: string
    thread_pool: string
  }
  metrics?: {
    total_requests: number
    successful_requests: number
    failed_requests: number
    cache_hit_rate: number
    average_response_time_ms: number
  }
}
