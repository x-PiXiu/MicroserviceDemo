<template>
  <div class="tier-badge" :class="[sizeClass, tierGroupClass]" :style="badgeStyle">
    <span class="tier-icon">{{ tierInfo?.icon }}</span>
    <span class="tier-name" v-if="showName">{{ displayName }}</span>
    <span class="tier-rating" v-if="showRating && rating !== undefined">({{ rating }})</span>
  </div>
</template>

<script setup lang="ts">
/**
 * 段位徽章组件
 *
 * 文档依据: game_platform_gameplay_design.md
 * - §4.2.2 段位划分表
 *
 * 功能：
 * - 根据段位或评分显示对应徽章
 * - 支持不同尺寸
 * - 显示段位图标和名称
 */
import { computed } from 'vue'
import {
  Tier,
  getTierInfo,
  getTierByRating,
  TierConfig
} from '@/types/tier.types'

const props = withDefaults(defineProps<{
  /** 段位枚举值 */
  tier?: Tier | number
  /** 段位名称（用于覆盖默认名称） */
  name?: string
  /** 评分（自动计算段位） */
  rating?: number
  /** 尺寸 */
  size?: 'small' | 'medium' | 'large'
  /** 是否显示名称 */
  showName?: boolean
  /** 是否显示评分 */
  showRating?: boolean
  /** 是否使用简洁模式（只显示图标） */
  compact?: boolean
}>(), {
  size: 'medium',
  showName: true,
  showRating: false,
  compact: false
})

/**
 * 计算实际段位
 * 优先使用 tier 属性，其次使用 rating 计算
 */
const actualTier = computed((): Tier => {
  if (props.tier !== undefined) {
    return typeof props.tier === 'number' ? (props.tier as Tier) : props.tier
  }
  if (props.rating !== undefined) {
    return getTierByRating(props.rating)
  }
  return Tier.BRONZE_III
})

/**
 * 获取段位配置
 */
const tierInfo = computed((): TierConfig | undefined => {
  return getTierInfo(actualTier.value)
})

/**
 * 显示名称（优先使用 props.name，其次使用配置名称）
 */
const displayName = computed((): string => {
  return props.name || tierInfo.value?.name || '未知'
})

/**
 * 尺寸类名
 */
const sizeClass = computed((): string => {
  return `size-${props.size}`
})

/**
 * 段位组类名（用于不同颜色主题）
 */
const tierGroupClass = computed((): string => {
  const group = tierInfo.value?.tierGroup || '青铜'
  const groupMap: Record<string, string> = {
    '青铜': 'tier-bronze',
    '白银': 'tier-silver',
    '黄金': 'tier-gold',
    '铂金': 'tier-platinum',
    '钻石': 'tier-diamond',
    '大师': 'tier-master'
  }
  return groupMap[group] || 'tier-bronze'
})

/**
 * 徽章样式（使用 CSS 变量）
 */
const badgeStyle = computed((): Record<string, string> => {
  const color = tierInfo.value?.color || '#CD7F32'
  return {
    '--tier-color': color,
    '--tier-color-light': `${color}33`,
    '--tier-color-dark': `${color}cc`
  }
})
</script>

<style scoped>
.tier-badge {
  display: inline-flex;
  align-items: center;
  gap: 0.375rem;
  padding: 0.25rem 0.625rem;
  background: linear-gradient(
    135deg,
    var(--tier-color) 0%,
    var(--tier-color-dark) 100%
  );
  border: 1px solid var(--tier-color-light);
  border-radius: 6px;
  color: white;
  font-weight: 600;
  text-shadow: 0 1px 2px rgba(0, 0, 0, 0.3);
  box-shadow:
    0 2px 4px rgba(0, 0, 0, 0.2),
    inset 0 1px 0 rgba(255, 255, 255, 0.2);
  transition: transform 0.2s ease, box-shadow 0.2s ease;
}

.tier-badge:hover {
  transform: translateY(-1px);
  box-shadow:
    0 4px 8px rgba(0, 0, 0, 0.3),
    inset 0 1px 0 rgba(255, 255, 255, 0.2);
}

/* 尺寸变体 */
.size-small {
  font-size: 0.75rem;
  padding: 0.125rem 0.375rem;
  gap: 0.25rem;
}

.size-small .tier-icon {
  font-size: 0.875rem;
}

.size-medium {
  font-size: 0.875rem;
}

.size-medium .tier-icon {
  font-size: 1rem;
}

.size-large {
  font-size: 1rem;
  padding: 0.5rem 0.875rem;
  gap: 0.5rem;
}

.size-large .tier-icon {
  font-size: 1.25rem;
}

/* 段位图标 */
.tier-icon {
  line-height: 1;
}

/* 段位名称 */
.tier-name {
  white-space: nowrap;
}

/* 评分显示 */
.tier-rating {
  opacity: 0.85;
  font-weight: 400;
  margin-left: 0.25rem;
}

/* 段位组特殊样式 */
.tier-bronze {
  background: linear-gradient(135deg, #CD7F32 0%, #8B4513 100%);
}

.tier-silver {
  background: linear-gradient(135deg, #C0C0C0 0%, #808080 100%);
}

.tier-gold {
  background: linear-gradient(135deg, #FFD700 0%, #B8860B 100%);
}

.tier-platinum {
  background: linear-gradient(135deg, #E5E4E2 0%, #A9A9A9 100%);
}

.tier-diamond {
  background: linear-gradient(135deg, #B9F2FF 0%, #00BFFF 100%);
}

.tier-master {
  background: linear-gradient(135deg, #FF6B6B 0%, #DC143C 100%);
  animation: master-glow 2s ease-in-out infinite;
}

@keyframes master-glow {
  0%, 100% {
    box-shadow:
      0 2px 4px rgba(220, 20, 60, 0.4),
      inset 0 1px 0 rgba(255, 255, 255, 0.3);
  }
  50% {
    box-shadow:
      0 4px 12px rgba(220, 20, 60, 0.6),
      inset 0 1px 0 rgba(255, 255, 255, 0.3),
      0 0 20px rgba(255, 107, 107, 0.4);
  }
}
</style>
