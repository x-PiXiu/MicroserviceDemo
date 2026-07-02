export const validators = {
  username: (value: string): boolean => {
    // 放宽验证：允许 3-50 个字符，与后端一致
    return value.length >= 3 && value.length <= 50
  },

  email: (value: string): boolean => {
    return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)
  },

  password: (value: string): boolean => {
    // 与后端一致：6-128 字符，至少包含1个数字
    return value.length >= 6 && value.length <= 128 && /\d/.test(value)
  },

  url: (value: string): boolean => {
    try {
      new URL(value)
      return true
    } catch {
      return false
    }
  },
}

export const errorMessages = {
  username: '用户名应为3-50个字符',
  email: '请输入有效的邮箱地址',
  password: '密码长度应为6-128个字符，且至少包含1个数字',
  url: '请输入有效的URL地址',
}
