<template>
  <div class="currency-display" :class="[`mode-${mode}`, `size-${size}`]">
    <div
      v-for="currency in displayCurrencies"
      :key="currency.type"
      class="currency-item"
      :class="{ updating: currency.updating }"
      @click="handleClick(currency.type)"
    >
      <div class="currency-icon" :class="`currency-${currency.type}`">
        <component :is="currency.icon" />
      </div>
      <div class="currency-info">
        <span class="currency-amount" :class="{ positive: currency.change > 0, negative: currency.change < 0 }">
          {{ formatAmount(currency.amount) }}
        </span>
        <span v-if="showChange && currency.change !== 0" class="currency-change" :class="{ positive: currency.change > 0 }">
          {{ currency.change > 0 ? '+' : '' }}{{ formatAmount(currency.change) }}
        </span>
      </div>
      <el-tooltip v-if="showTooltip" :content="currency.tooltip" placement="top">
        <el-icon class="info-icon"><InfoFilled /></el-icon>
      </el-tooltip>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, type Component } from 'vue'
import {
  Coin,
  Present,
  Medal,
  StarFilled,
  InfoFilled,
} from '@element-plus/icons-vue'
import { useGameDataStore } from '@/stores/gamedata.store'
import type { CurrencyType } from '@/types'

const props = withDefaults(
  defineProps<{
    /** 显示模式: compact(紧凑) / full(完整) / inline(行内) */
    mode?: 'compact' | 'full' | 'inline'
    /** 尺寸: small / medium / large */
    size?: 'small' | 'medium' | 'large'
    /** 指定显示的货币类型，不指定则显示全部 */
    types?: CurrencyType[]
    /** 是否显示变化值 */
    showChange?: boolean
    /** 是否显示提示 */
    showTooltip?: boolean
  }>(),
  {
    mode: 'compact',
    size: 'medium',
    types: undefined,
    showChange: false,
    showTooltip: false,
  }
)

const emit = defineEmits<{
  (e: 'click', type: CurrencyType): void
}>()

const gameDataStore = useGameDataStore()

// 货币配置（使用文档标准名称）
const currencyConfig: Record<string, { icon: Component; label: string; tooltip: string }> = {
  // 文档标准货币类型
  gold: {
    icon: Coin,
    label: '金币',
    tooltip: '金币 - 用于购买基础道具',
  },
  gem: {
    icon: Present,
    label: '宝石',
    tooltip: '宝石 - 稀有货币，用于购买高级道具',
  },
  honor: {
    icon: Medal,
    label: '荣誉值',
    tooltip: '荣誉值 - 通过竞技获得，用于解锁特殊内容',
  },
  // 向后兼容的旧货币类型
  coins: {
    icon: Coin,
    label: '金币',
    tooltip: '金币 - 用于购买基础道具',
  },
  gems: {
    icon: Present,
    label: '宝石',
    tooltip: '宝石 - 稀有货币，用于购买高级道具',
  },
  honor_points: {
    icon: Medal,
    label: '荣誉值',
    tooltip: '荣誉值 - 通过竞技获得，用于解锁特殊内容',
  },
  tokens: {
    icon: StarFilled,
    label: '代币',
    tooltip: '代币 - 限时活动货币',
  },
}

// 动画状态
const updatingTypes = ref<Set<CurrencyType>>(new Set())
const changeValues = ref<Record<CurrencyType, number>>({} as Record<CurrencyType, number>)

// 监听货币变化
watch(
  () => gameDataStore.currencies,
  (newCurrencies, oldCurrencies) => {
    newCurrencies.forEach((newCurrency) => {
      // 大小写不敏感匹配
      const oldCurrency = oldCurrencies?.find(
        (c) => c.currency_type.toLowerCase() === newCurrency.currency_type.toLowerCase()
      )
      if (oldCurrency && oldCurrency.amount !== newCurrency.amount) {
        // 统一转换为小写类型，确保 changeValues 的键一致
        const type = newCurrency.currency_type.toLowerCase() as CurrencyType
        const change = newCurrency.amount - oldCurrency.amount

        // 记录变化值
        changeValues.value[type] = change

        // 触发动画
        updatingTypes.value.add(type)
        setTimeout(() => {
          updatingTypes.value.delete(type)
        }, 500)

        // 延迟清除变化值
        setTimeout(() => {
          changeValues.value[type] = 0
        }, 3000)
      }
    })
  },
  { deep: true }
)

// 计算要显示的货币列表
const displayCurrencies = computed(() => {
  // 默认显示文档标准货币类型
  const types = props.types || (['gold', 'gem', 'honor'] as CurrencyType[])

  return types
    .filter((type) => currencyConfig[type])
    .map((type) => {
      const config = currencyConfig[type]
      // 大小写不敏感匹配（服务端返回大写 GOLD，客户端使用小写 gold）
      const normalizedType = type.toLowerCase()
      const currency = gameDataStore.currencies.find((c) =>
        c.currency_type.toLowerCase() === normalizedType
      )

      return {
        type,
        icon: config.icon,
        label: config.label,
        tooltip: config.tooltip,
        amount: currency?.amount || 0,
        change: changeValues.value[type] || 0,
        updating: updatingTypes.value.has(type),
      }
    })
})

// 格式化金额
const formatAmount = (amount: number): string => {
  if (amount >= 1000000) {
    return (amount / 1000000).toFixed(1) + 'M'
  }
  if (amount >= 1000) {
    return (amount / 1000).toFixed(1) + 'K'
  }
  return amount.toLocaleString()
}

const handleClick = (type: CurrencyType) => {
  emit('click', type)
}
</script>

<style scoped>
.currency-display {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.currency-display.mode-inline {
  gap: 0.75rem;
}

.currency-display.mode-full {
  flex-direction: column;
  gap: 0.75rem;
}

.currency-item {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.25rem 0.5rem;
  border-radius: 8px;
  background: var(--el-fill-color-light);
  transition: all 0.3s;
  cursor: default;
}

.currency-display.mode-full .currency-item {
  padding: 0.5rem 1rem;
  background: var(--el-fill-color-blank);
  border: 1px solid var(--el-border-color-light);
}

.currency-item:hover {
  background: var(--el-fill-color);
}

.currency-item.updating {
  animation: pulse 0.5s ease-in-out;
}

@keyframes pulse {
  0%,
  100% {
    transform: scale(1);
  }
  50% {
    transform: scale(1.05);
  }
}

.currency-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 24px;
  height: 24px;
  border-radius: 50%;
  font-size: 14px;
}

.currency-display.size-small .currency-icon {
  width: 20px;
  height: 20px;
  font-size: 12px;
}

.currency-display.size-large .currency-icon {
  width: 32px;
  height: 32px;
  font-size: 18px;
}

.currency-icon.currency-gold,
.currency-icon.currency-coins {
  background: linear-gradient(135deg, #ffd700 0%, #ffb700 100%);
  color: #fff;
}

.currency-icon.currency-gem,
.currency-icon.currency-gems {
  background: linear-gradient(135deg, #a855f7 0%, #7c3aed 100%);
  color: #fff;
}

.currency-icon.currency-honor,
.currency-icon.currency-honor_points {
  background: linear-gradient(135deg, #3b82f6 0%, #1d4ed8 100%);
  color: #fff;
}

.currency-icon.currency-tokens {
  background: linear-gradient(135deg, #10b981 0%, #059669 100%);
  color: #fff;
}

.currency-info {
  display: flex;
  flex-direction: column;
  min-width: 40px;
}

.currency-display.mode-inline .currency-info {
  flex-direction: row;
  align-items: center;
  gap: 0.25rem;
}

.currency-amount {
  font-weight: 600;
  font-size: 0.875rem;
  color: var(--el-text-color-primary);
  transition: color 0.3s;
}

.currency-display.size-small .currency-amount {
  font-size: 0.75rem;
}

.currency-display.size-large .currency-amount {
  font-size: 1rem;
}

.currency-amount.positive {
  color: #67c23a;
}

.currency-amount.negative {
  color: #f56c6c;
}

.currency-change {
  font-size: 0.7rem;
  font-weight: 500;
  color: #f56c6c;
  animation: fadeIn 0.3s ease-in-out;
}

.currency-display.mode-full .currency-change {
  font-size: 0.75rem;
}

.currency-change.positive {
  color: #67c23a;
}

@keyframes fadeIn {
  from {
    opacity: 0;
    transform: translateY(-5px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.info-icon {
  font-size: 12px;
  color: var(--el-text-color-secondary);
  cursor: help;
  opacity: 0;
  transition: opacity 0.2s;
}

.currency-item:hover .info-icon {
  opacity: 1;
}
</style>
