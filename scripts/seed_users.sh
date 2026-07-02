#!/bin/bash
# 批量创建测试用户（通过 user_service:8082 直接创建，跳过邮箱验证）
# 用法: ./scripts/seed_users.sh [用户数量，默认1000]
# 密码统一为: Test@12345

USER_SERVICE_URL="http://localhost:8082/api/v1/user"
USER_COUNT=${1:-1000}
PASSWORD="Test@12345"

# 生成 BCrypt 哈希（cost=12）
generate_bcrypt() {
    local pw="$1"
    # 方法1: python3 + bcrypt
    if command -v python3 &>/dev/null; then
        local hash
        hash=$(python3 -c "
import bcrypt
print(bcrypt.hashpw(b'$pw', bcrypt.gensalt(12)).decode('utf-8'))
" 2>/dev/null)
        if [ -n "$hash" ]; then
            echo "$hash"
            return 0
        fi
    fi
    # 方法2: python + bcrypt
    if command -v python &>/dev/null; then
        local hash
        hash=$(python -c "
import bcrypt
print(bcrypt.hashpw(b'$pw', bcrypt.gensalt(12)).decode('utf-8'))
" 2>/dev/null)
        if [ -n "$hash" ]; then
            echo "$hash"
            return 0
        fi
    fi
    return 1
}

echo "=== 批量创建测试用户 ==="
echo "目标: $USER_COUNT 个用户"
echo "目标服务: $USER_SERVICE_URL"
echo ""

# 尝试安装 bcrypt 模块
python3 -c "import bcrypt" 2>/dev/null || {
    echo "安装 python3 bcrypt 模块..."
    pip3 install bcrypt 2>/dev/null || pip install bcrypt 2>/dev/null
}

echo "生成 BCrypt 密码哈希..."
BCRYPT_HASH=$(generate_bcrypt "$PASSWORD")
if [ -z "$BCRYPT_HASH" ]; then
    echo "ERROR: 无法生成 BCrypt 哈希，请先安装: pip3 install bcrypt"
    exit 1
fi
echo "哈希: ${BCRYPT_HASH:0:30}..."
echo ""

SUCCESS=0
FAIL=0

for i in $(seq 1 $USER_COUNT); do
  RESPONSE=$(curl -s -w "\n%{http_code}" -X POST "$USER_SERVICE_URL" \
    -H "Content-Type: application/json" \
    -d "{
      \"user_id\": \"lt_user_$i\",
      \"username\": \"loadtest_user_$i\",
      \"email\": \"loadtest_$i@test.com\",
      \"password_hash\": \"$BCRYPT_HASH\",
      \"status\": 0
    }")

  HTTP_CODE=$(echo "$RESPONSE" | tail -1)
  BODY=$(echo "$RESPONSE" | head -n -1)

  if [ "$HTTP_CODE" = "201" ]; then
    SUCCESS=$((SUCCESS + 1))
  else
    FAIL=$((FAIL + 1))
    if [ $FAIL -le 3 ]; then
      echo "WARN: 用户 $i 创建失败 (HTTP $HTTP_CODE): $BODY"
    fi
  fi

  if [ $((i % 100)) -eq 0 ]; then
    echo "进度: $i / $USER_COUNT (成功: $SUCCESS, 失败: $FAIL)"
  fi
done

echo ""
echo "=== 创建完成 ==="
echo "成功: $SUCCESS / $USER_COUNT"
echo "密码: $PASSWORD"
