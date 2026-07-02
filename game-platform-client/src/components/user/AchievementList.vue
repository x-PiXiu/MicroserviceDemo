<template>
  <div class="achievement-list">
    <div class="achievement-header">
      <div class="header-title">
        <h2>成就系统</h2>
        <div class="achievement-stats">
          <el-statistic title="已解锁成就" :value="unlockedCount" />
          <el-statistic title="总积分" :value="totalPoints" />
        </div>
      </div>
      <div class="header-filters">
        <el-select
          v-model="selectedType"
          placeholder="成就类型"
          clearable
          style="width: 150px"
          @change="handleTypeChange"
        >
          <el-option
            v-for="type in availableTypes"
            :key="type"
            :label="type"
            :value="type"
          />
        </el-select>
      </div>
    </div>

    <div class="achievement-content">
      <div v-if="loading" class="loading-state">
        <el-skeleton :rows="5" animated />
      </div>
      <div v-else-if="filteredAchievements.length === 0" class="empty-state">
        <el-empty :description="selectedType ? '该类型暂无成就' : '暂无已解锁的成就'" />
      </div>
      <div v-else class="achievements-grid">
        <el-card
          v-for="achievement in filteredAchievements"
          :key="achievement.achievement_id"
          class="achievement-card"
          shadow="hover"
        >
          <div class="achievement-card-content">
            <div class="achievement-icon">
              <el-icon :size="32" color="#f59e0b"><Trophy /></el-icon>
            </div>
            <div class="achievement-info">
              <h3>{{ achievement.title }}</h3>
              <p class="achievement-description">{{ achievement.description }}</p>
              <div class="achievement-meta">
                <el-tag size="small" type="info">{{ achievement.achievement_type }}</el-tag>
                <span class="achievement-points">
                  <el-icon><Star /></el-icon>
                  {{ achievement.points }} 积分
                </span>
              </div>
            </div>
          </div>
        </el-card>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted } from 'vue'
import { storeToRefs } from 'pinia'
import { Trophy, Star } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import { useAchievementStore, useAuthStore } from '@/stores'

const achievementStore = useAchievementStore()
const authStore = useAuthStore()

const selectedType = ref('')

// 使用 storeToRefs 解构响应式状态
const { achievements, unlockedCount, totalPoints, achievementsByType, loading } =
  storeToRefs(achievementStore)

// 直接从 store 获取 actions
const { getUserAchievements } = achievementStore

const availableTypes = computed(() => Object.keys(achievementsByType.value))

const filteredAchievements = computed(() => {
  if (!selectedType.value) {
    return achievements.value
  }
  return achievementsByType.value[selectedType.value] || []
})

const handleTypeChange = () => {
  // Filter is handled by computed property
}

onMounted(async () => {
  const userId = authStore.user?.id
  if (!userId) {
    ElMessage.warning('请先登录')
    return
  }

  // 如果 gameDataStore.loadAllGameData 已同步过成就数据，则跳过重复请求
  if (achievements.value.length > 0) {
    return
  }

  try {
    await getUserAchievements(userId)
  } catch (error) {
    ElMessage.error('加载成就列表失败')
  }
})
</script>

<style scoped>
.achievement-list {
  padding: 1rem;
}

.achievement-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 2rem;
  flex-wrap: wrap;
  gap: 1rem;
}

.header-title h2 {
  margin: 0 0 1rem 0;
  font-size: 1.5rem;
}

.achievement-stats {
  display: flex;
  gap: 2rem;
}

.header-filters {
  display: flex;
  gap: 0.5rem;
}

.achievement-content {
  background: var(--el-bg-color);
  border-radius: 8px;
  padding: 1.5rem;
  min-height: 300px;
}

.loading-state,
.empty-state {
  padding: 3rem;
  text-align: center;
}

.achievements-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 1rem;
}

.achievement-card {
  transition: transform 0.2s;
}

.achievement-card:hover {
  transform: translateY(-2px);
}

.achievement-card-content {
  display: flex;
  gap: 1rem;
}

.achievement-icon {
  width: 60px;
  height: 60px;
  border-radius: 50%;
  background: linear-gradient(135deg, #fef3c7 0%, #fde68a 100%);
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.achievement-info {
  flex: 1;
  min-width: 0;
}

.achievement-info h3 {
  margin: 0 0 0.5rem 0;
  font-size: 1rem;
  color: var(--el-text-color-primary);
}

.achievement-description {
  margin: 0 0 0.75rem 0;
  font-size: 0.875rem;
  color: var(--el-text-color-regular);
  line-height: 1.4;
}

.achievement-meta {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  margin-bottom: 0.5rem;
}

.achievement-points {
  display: flex;
  align-items: center;
  gap: 0.25rem;
  font-size: 0.875rem;
  color: #f59e0b;
  font-weight: 500;
}

@media (max-width: 768px) {
  .achievement-header {
    flex-direction: column;
  }

  .achievement-stats {
    width: 100%;
    justify-content: space-around;
  }

  .header-filters {
    width: 100%;
  }

  .achievements-grid {
    grid-template-columns: 1fr;
  }
}
</style>
