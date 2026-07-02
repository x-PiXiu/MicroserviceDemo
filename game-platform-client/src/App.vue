<template>
  <div id="app" :class="{ dark: isDark }">
    <router-view />
  </div>
</template>

<script setup lang="ts">
import { useDark } from '@vueuse/core'

const isDark = useDark()
</script>

<style>
#app {
  min-height: 100vh;
  background-color: var(--el-bg-color);
  color: var(--el-text-color-primary);
  transition: background-color 0.3s, color 0.3s;
}
</style>
