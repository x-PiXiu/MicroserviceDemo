import { ref, onMounted, onUnmounted } from 'vue'
import { BREAKPOINTS } from '@/utils'

export function useBreakpoint() {
  const width = ref(window.innerWidth)

  const handleResize = () => {
    width.value = window.innerWidth
  }

  onMounted(() => {
    window.addEventListener('resize', handleResize)
  })

  onUnmounted(() => {
    window.removeEventListener('resize', handleResize)
  })

  const is = {
    xs: () => width.value < BREAKPOINTS.sm,
    sm: () => width.value >= BREAKPOINTS.sm && width.value < BREAKPOINTS.md,
    md: () => width.value >= BREAKPOINTS.md && width.value < BREAKPOINTS.lg,
    lg: () => width.value >= BREAKPOINTS.lg && width.value < BREAKPOINTS.xl,
    xl: () => width.value >= BREAKPOINTS.xl && width.value < BREAKPOINTS['2xl'],
    '2xl': () => width.value >= BREAKPOINTS['2xl'],
  }

  const gt = {
    xs: () => width.value >= BREAKPOINTS.sm,
    sm: () => width.value >= BREAKPOINTS.md,
    md: () => width.value >= BREAKPOINTS.lg,
    lg: () => width.value >= BREAKPOINTS.xl,
    xl: () => width.value >= BREAKPOINTS['2xl'],
  }

  const lt = {
    sm: () => width.value < BREAKPOINTS.sm,
    md: () => width.value < BREAKPOINTS.md,
    lg: () => width.value < BREAKPOINTS.lg,
    xl: () => width.value < BREAKPOINTS.xl,
    '2xl': () => width.value < BREAKPOINTS['2xl'],
  }

  return {
    width,
    is,
    gt,
    lt,
  }
}
