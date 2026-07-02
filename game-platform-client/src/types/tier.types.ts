/**
 * 段位系统类型定义
 *
 * 文档依据: game_platform_gameplay_design.md
 * - §4.2.1 ELO 评分机制
 * - §4.2.2 段位划分表（16 个段位）
 * - §14.1.1 EloRatingCalculator 段位枚举
 */

/**
 * 段位枚举
 * 与服务端 elo_rating_calculator.h 中的 Tier 枚举保持一致
 */
export enum Tier {
  BRONZE_III = 0,
  BRONZE_II = 1,
  BRONZE_I = 2,
  SILVER_III = 3,
  SILVER_II = 4,
  SILVER_I = 5,
  GOLD_III = 6,
  GOLD_II = 7,
  GOLD_I = 8,
  PLATINUM_III = 9,
  PLATINUM_II = 10,
  PLATINUM_I = 11,
  DIAMOND_III = 12,
  DIAMOND_II = 13,
  DIAMOND_I = 14,
  MASTER = 15
}

/**
 * 段位配置接口
 */
export interface TierConfig {
  name: string           // 中文名称，如 "黄金 II"
  nameEn: string         // 英文名称，如 "Gold II"
  icon: string           // 显示图标
  color: string          // 主题颜色
  minRating: number      // 最低评分
  maxRating: number      // 最高评分
  tierGroup: string      // 段位组（青铜/白银/黄金/铂金/钻石/大师）
}

/**
 * 段位配置表
 * 文档依据: §4.2.2 段位划分表
 */
export const TIER_CONFIG: Record<Tier, TierConfig> = {
  [Tier.BRONZE_III]: {
    name: '青铜 III',
    nameEn: 'Bronze III',
    icon: '🥉',
    color: '#CD7F32',
    minRating: 100,
    maxRating: 399,
    tierGroup: '青铜'
  },
  [Tier.BRONZE_II]: {
    name: '青铜 II',
    nameEn: 'Bronze II',
    icon: '🥉',
    color: '#CD7F32',
    minRating: 400,
    maxRating: 599,
    tierGroup: '青铜'
  },
  [Tier.BRONZE_I]: {
    name: '青铜 I',
    nameEn: 'Bronze I',
    icon: '🥉',
    color: '#CD7F32',
    minRating: 600,
    maxRating: 799,
    tierGroup: '青铜'
  },
  [Tier.SILVER_III]: {
    name: '白银 III',
    nameEn: 'Silver III',
    icon: '🥈',
    color: '#C0C0C0',
    minRating: 800,
    maxRating: 999,
    tierGroup: '白银'
  },
  [Tier.SILVER_II]: {
    name: '白银 II',
    nameEn: 'Silver II',
    icon: '🥈',
    color: '#C0C0C0',
    minRating: 1000,
    maxRating: 1199,
    tierGroup: '白银'
  },
  [Tier.SILVER_I]: {
    name: '白银 I',
    nameEn: 'Silver I',
    icon: '🥈',
    color: '#C0C0C0',
    minRating: 1200,
    maxRating: 1399,
    tierGroup: '白银'
  },
  [Tier.GOLD_III]: {
    name: '黄金 III',
    nameEn: 'Gold III',
    icon: '🥇',
    color: '#FFD700',
    minRating: 1400,
    maxRating: 1599,
    tierGroup: '黄金'
  },
  [Tier.GOLD_II]: {
    name: '黄金 II',
    nameEn: 'Gold II',
    icon: '🥇',
    color: '#FFD700',
    minRating: 1600,
    maxRating: 1799,
    tierGroup: '黄金'
  },
  [Tier.GOLD_I]: {
    name: '黄金 I',
    nameEn: 'Gold I',
    icon: '🥇',
    color: '#FFD700',
    minRating: 1800,
    maxRating: 1999,
    tierGroup: '黄金'
  },
  [Tier.PLATINUM_III]: {
    name: '铂金 III',
    nameEn: 'Platinum III',
    icon: '💎',
    color: '#E5E4E2',
    minRating: 2000,
    maxRating: 2199,
    tierGroup: '铂金'
  },
  [Tier.PLATINUM_II]: {
    name: '铂金 II',
    nameEn: 'Platinum II',
    icon: '💎',
    color: '#E5E4E2',
    minRating: 2200,
    maxRating: 2399,
    tierGroup: '铂金'
  },
  [Tier.PLATINUM_I]: {
    name: '铂金 I',
    nameEn: 'Platinum I',
    icon: '💎',
    color: '#E5E4E2',
    minRating: 2400,
    maxRating: 2599,
    tierGroup: '铂金'
  },
  [Tier.DIAMOND_III]: {
    name: '钻石 III',
    nameEn: 'Diamond III',
    icon: '💠',
    color: '#B9F2FF',
    minRating: 2600,
    maxRating: 2799,
    tierGroup: '钻石'
  },
  [Tier.DIAMOND_II]: {
    name: '钻石 II',
    nameEn: 'Diamond II',
    icon: '💠',
    color: '#B9F2FF',
    minRating: 2800,
    maxRating: 2899,
    tierGroup: '钻石'
  },
  [Tier.DIAMOND_I]: {
    name: '钻石 I',
    nameEn: 'Diamond I',
    icon: '💠',
    color: '#B9F2FF',
    minRating: 2900,
    maxRating: 2999,
    tierGroup: '钻石'
  },
  [Tier.MASTER]: {
    name: '大师',
    nameEn: 'Master',
    icon: '👑',
    color: '#FF4500',  // 文档标准颜色
    minRating: 3000,
    maxRating: 9999,
    tierGroup: '大师'
  }
}

/**
 * 段位组配置
 */
export const TIER_GROUPS = [
  { name: '青铜', color: '#CD7F32', icon: '🥉', tiers: [Tier.BRONZE_III, Tier.BRONZE_II, Tier.BRONZE_I] },
  { name: '白银', color: '#C0C0C0', icon: '🥈', tiers: [Tier.SILVER_III, Tier.SILVER_II, Tier.SILVER_I] },
  { name: '黄金', color: '#FFD700', icon: '🥇', tiers: [Tier.GOLD_III, Tier.GOLD_II, Tier.GOLD_I] },
  { name: '铂金', color: '#E5E4E2', icon: '💎', tiers: [Tier.PLATINUM_III, Tier.PLATINUM_II, Tier.PLATINUM_I] },
  { name: '钻石', color: '#B9F2FF', icon: '💠', tiers: [Tier.DIAMOND_III, Tier.DIAMOND_II, Tier.DIAMOND_I] },
  { name: '大师', color: '#FF4500', icon: '👑', tiers: [Tier.MASTER] }  // 文档标准颜色
]

/**
 * 根据评分获取段位
 * 文档依据: §4.2.2 段位划分表
 *
 * @param rating - 玩家评分
 * @returns 对应的段位枚举值
 */
export function getTierByRating(rating: number): Tier {
  if (rating >= 3000) return Tier.MASTER
  if (rating >= 2900) return Tier.DIAMOND_I
  if (rating >= 2800) return Tier.DIAMOND_II
  if (rating >= 2600) return Tier.DIAMOND_III
  if (rating >= 2400) return Tier.PLATINUM_I
  if (rating >= 2200) return Tier.PLATINUM_II
  if (rating >= 2000) return Tier.PLATINUM_III
  if (rating >= 1800) return Tier.GOLD_I
  if (rating >= 1600) return Tier.GOLD_II
  if (rating >= 1400) return Tier.GOLD_III
  if (rating >= 1200) return Tier.SILVER_I
  if (rating >= 1000) return Tier.SILVER_II
  if (rating >= 800) return Tier.SILVER_III
  if (rating >= 600) return Tier.BRONZE_I
  if (rating >= 400) return Tier.BRONZE_II
  return Tier.BRONZE_III
}

/**
 * 获取段位配置信息
 *
 * @param tier - 段位枚举值
 * @returns 段位配置
 */
export function getTierInfo(tier: Tier): TierConfig {
  return TIER_CONFIG[tier]
}

/**
 * 根据评分直接获取段位配置
 *
 * @param rating - 玩家评分
 * @returns 段位配置
 */
export function getTierInfoByRating(rating: number): TierConfig {
  const tier = getTierByRating(rating)
  return getTierInfo(tier)
}

/**
 * 获取下一个段位（如果存在）
 *
 * @param tier - 当前段位
 * @returns 下一个段位，如果已是最高段位则返回 null
 */
export function getNextTier(tier: Tier): Tier | null {
  if (tier === Tier.MASTER) return null
  return (tier + 1) as Tier
}

/**
 * 获取上一个段位（如果存在）
 *
 * @param tier - 当前段位
 * @returns 上一个段位，如果已是最低段位则返回 null
 */
export function getPreviousTier(tier: Tier): Tier | null {
  if (tier === Tier.BRONZE_III) return null
  return (tier - 1) as Tier
}

/**
 * 计算到下一段位需要的评分
 * 文档依据: §4.2.2 段位划分表
 *
 * @param rating - 当前评分
 * @returns 需要的评分和当前评分
 */
export function getRatingToNextTier(rating: number): {
  needed: number       // 到下一段位需要的评分
  current: number      // 当前评分
  nextTier: Tier | null // 下一个段位
  currentTier: Tier    // 当前段位
  progress: number     // 当前段位内的进度百分比 (0-100)
} {
  const currentTier = getTierByRating(rating)
  const nextTier = getNextTier(currentTier)
  const currentConfig = TIER_CONFIG[currentTier]

  // 计算当前段位内的进度
  const tierRange = currentConfig.maxRating - currentConfig.minRating + 1
  const progressInTier = rating - currentConfig.minRating
  const progress = Math.min(100, Math.max(0, (progressInTier / tierRange) * 100))

  if (!nextTier) {
    return {
      needed: 0,
      current: rating,
      nextTier: null,
      currentTier,
      progress: 100
    }
  }

  const nextTierConfig = TIER_CONFIG[nextTier]
  return {
    needed: nextTierConfig.minRating - rating,
    current: rating,
    nextTier,
    currentTier,
    progress
  }
}

/**
 * 判断段位是否发生变化
 *
 * @param ratingBefore - 之前的评分
 * @param ratingAfter - 之后的评分
 * @returns 段位变化信息
 */
export function checkTierChange(ratingBefore: number, ratingAfter: number): {
  changed: boolean
  tierBefore: Tier
  tierAfter: Tier
  isUpgrade: boolean
  isDowngrade: boolean
} {
  const tierBefore = getTierByRating(ratingBefore)
  const tierAfter = getTierByRating(ratingAfter)

  return {
    changed: tierBefore !== tierAfter,
    tierBefore,
    tierAfter,
    isUpgrade: tierAfter > tierBefore,
    isDowngrade: tierAfter < tierBefore
  }
}

/**
 * 格式化段位名称显示
 *
 * @param tier - 段位枚举值或数字
 * @param showIcon - 是否显示图标
 * @returns 格式化后的段位字符串
 */
export function formatTierName(tier: Tier | number, showIcon: boolean = true): string {
  const tierNum = typeof tier === 'number' ? tier as Tier : tier
  const config = TIER_CONFIG[tierNum]
  if (!config) return '未知'
  return showIcon ? `${config.icon} ${config.name}` : config.name
}

/**
 * 英文段位名称到中文的映射
 * 服务端返回英文名称（如 "Gold II"），客户端需要转换为中文显示
 */
export const TIER_EN_TO_CN: Record<string, string> = {
  'Bronze III': '青铜 III',
  'Bronze II': '青铜 II',
  'Bronze I': '青铜 I',
  'Silver III': '白银 III',
  'Silver II': '白银 II',
  'Silver I': '白银 I',
  'Gold III': '黄金 III',
  'Gold II': '黄金 II',
  'Gold I': '黄金 I',
  'Platinum III': '铂金 III',
  'Platinum II': '铂金 II',
  'Platinum I': '铂金 I',
  'Diamond III': '钻石 III',
  'Diamond II': '钻石 II',
  'Diamond I': '钻石 I',
  'Master': '大师'
}

/**
 * 将服务端返回的英文段位名称转换为中文
 *
 * @param tierNameEn - 英文段位名称（如 "Gold II"）
 * @returns 中文段位名称（如 "黄金 II"）
 */
export function tierNameToChinese(tierNameEn: string): string {
  return TIER_EN_TO_CN[tierNameEn] || tierNameEn
}

/**
 * 根据段位等级获取中文段位名称
 *
 * @param tierLevel - 段位等级 (0-15)
 * @returns 中文段位名称
 */
export function getTierNameByLevel(tierLevel: number): string {
  if (tierLevel < 0 || tierLevel > 15) return '青铜 III'
  return TIER_CONFIG[tierLevel as Tier].name
}

/**
 * 根据段位等级获取英文段位名称
 *
 * @param tierLevel - 段位等级 (0-15)
 * @returns 英文段位名称
 */
export function getTierNameEnByLevel(tierLevel: number): string {
  if (tierLevel < 0 || tierLevel > 15) return 'Bronze III'
  return TIER_CONFIG[tierLevel as Tier].nameEn
}
