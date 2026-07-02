import { useDark, useToggle } from '@vueuse/core'
import { useStorage } from './useStorage'

export function useTheme() {
  const isDark = useDark()
  const toggleTheme = useToggle(isDark)
  const theme = useStorage('theme', 'auto')

  const setTheme = (newTheme: 'light' | 'dark' | 'auto') => {
    theme.value = newTheme
    if (newTheme === 'auto') {
      isDark.value = window.matchMedia('(prefers-color-scheme: dark)').matches
    } else {
      isDark.value = newTheme === 'dark'
    }
  }

  return {
    isDark,
    theme,
    toggleTheme,
    setTheme,
  }
}
