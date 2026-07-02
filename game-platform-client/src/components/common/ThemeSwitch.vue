<template>
  <div class="theme-switch">
    <el-switch
      v-model="isDark"
      inline-prompt
      :active-icon="Moon"
      :inactive-icon="Sunny"
      @change="toggleTheme"
    />
  </div>
</template>

<script setup lang="ts">
import { useDark, useToggle } from '@vueuse/core'
import { Moon, Sunny } from '@element-plus/icons-vue'

const isDark = useDark()
const toggleTheme = useToggle(isDark)
</script>

<style scoped>
.theme-switch {
  display: flex;
  align-items: center;
}
</style>
