import { storage } from '@/utils'
import { ref, watch } from 'vue'

export function useStorage<T>(key: string, defaultValue: T) {
  const stored = storage.get<T>(key, defaultValue)
  const value = ref(stored !== null ? stored : defaultValue)

  watch(
    value,
    (newValue) => {
      storage.set(key, newValue)
    },
    { deep: true }
  )

  return value
}
