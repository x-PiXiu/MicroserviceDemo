/**
 * 前端密码强度检查器
 * 与后端 PasswordManager 的校验规则保持一致
 */

const COMMON_PASSWORDS = new Set([
  '123456', 'password', '123456789', '12345678', '12345',
  '1234567', '1234567890', 'qwerty', 'abc123', '111111',
  '123123', 'admin', 'letmein', 'welcome', 'monkey',
  'dragon', 'pass', 'master', 'hello', 'freedom',
  'whatever', 'qazwsx', 'trustno1', 'jordan', 'harley',
  '1234', 'robert', 'matthew', 'jordan23', '1qaz2wsx',
  'superman', '123qwe', 'football', 'baseball', 'shadow',
  '654321', 'master12', 'p@ssw0rd', 'password1', 'admin123',
])

interface PasswordRule {
  label: string
  passed: boolean
}

interface PasswordCheckResult {
  rules: PasswordRule[]
  level: 'weak' | 'medium' | 'strong'
  allPassed: boolean
}

export const passwordChecker = {
  check(password: string): PasswordCheckResult {
    const hasDigit = /\d/.test(password)
    const hasLower = /[a-z]/.test(password)
    const hasUpper = /[A-Z]/.test(password)
    const hasSpecial = /[!@#$%^&*()_+\-=\[\]{}|;:,.<>?]/.test(password)

    const lengthRule: PasswordRule = {
      label: '长度 6-128 个字符',
      passed: password.length >= 6 && password.length <= 128,
    }

    const digitRule: PasswordRule = {
      label: '至少包含 1 个数字',
      passed: hasDigit,
    }

    const commonRule: PasswordRule = {
      label: '不能使用常见密码（如 123456、password）',
      passed: !COMMON_PASSWORDS.has(password.toLowerCase()),
    }

    const charTypes = [hasLower, hasUpper, hasDigit, hasSpecial].filter(Boolean).length
    const strengthRule: PasswordRule = charTypes >= 3
      ? { label: '密码强度良好', passed: true }
      : { label: '建议混合使用大小写字母、数字和特殊字符', passed: false }

    const mandatoryPassed = lengthRule.passed && digitRule.passed && commonRule.passed
    const allPassed = mandatoryPassed && strengthRule.passed
    const rules = [lengthRule, digitRule, commonRule, strengthRule]

    let level: 'weak' | 'medium' | 'strong'
    if (!mandatoryPassed) {
      level = 'weak'
    } else if (charTypes >= 3 && password.length >= 8) {
      level = 'strong'
    } else {
      level = 'medium'
    }

    return { rules, level, allPassed }
  },
}
