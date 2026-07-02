import dayjs from 'dayjs'
import relativeTime from 'dayjs/plugin/relativeTime'
import 'dayjs/locale/zh-cn'
import 'dayjs/locale/en'

dayjs.extend(relativeTime)

export const formatDate = (date: string | Date, format = 'YYYY-MM-DD HH:mm:ss'): string => {
  return dayjs(date).format(format)
}

export const formatRelativeTime = (date: string | Date, locale = 'zh-cn'): string => {
  dayjs.locale(locale)
  return dayjs(date).fromNow()
}

export const formatDuration = (seconds: number): string => {
  const hours = Math.floor(seconds / 3600)
  const minutes = Math.floor((seconds % 3600) / 60)
  const secs = seconds % 60

  if (hours > 0) {
    return `${hours}:${String(minutes).padStart(2, '0')}:${String(secs).padStart(2, '0')}`
  }
  return `${minutes}:${String(secs).padStart(2, '0')}`
}

export const isToday = (date: string | Date): boolean => {
  return dayjs(date).isSame(dayjs(), 'day')
}

export const isYesterday = (date: string | Date): boolean => {
  return dayjs(date).isSame(dayjs().subtract(1, 'day'), 'day')
}
