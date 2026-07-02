<template>
  <el-dialog
    :model-value="modelValue"
    @update:model-value="handleClose"
    :title="title"
    :width="width"
    :before-close="handleBeforeClose"
  >
    <slot></slot>
    <template #footer v-if="showFooter">
      <el-button @click="handleClose">{{ cancelText || '取消' }}</el-button>
      <el-button type="primary" @click="handleConfirm" :loading="loading">
        {{ confirmText || '确认' }}
      </el-button>
    </template>
  </el-dialog>
</template>

<script setup lang="ts">
defineProps<{
  modelValue: boolean
  title?: string
  width?: string | number
  confirmText?: string
  cancelText?: string
  showFooter?: boolean
  loading?: boolean
}>()

const emit = defineEmits<{
  'update:modelValue': [value: boolean]
  confirm: []
}>()

const handleClose = () => {
  emit('update:modelValue', false)
}

const handleConfirm = () => {
  emit('confirm')
}

const handleBeforeClose = (done: () => void) => {
  done()
}
</script>
