#!/bin/bash
# 批量灌入成就数据
# 用法: ./scripts/seed_achievements.sh [用户数量，默认100]

TOKEN=${TOKEN:-"YOUR_ADMIN_TOKEN"}
BASE_URL="http://localhost:8084/api/v1/gamedata/achievements"
USER_COUNT=${1:-100}

ACHIEVEMENT_TYPES=("gameplay" "progression" "social" "special")
TITLES=("初次胜利" "连胜达人" "百战老兵" "社交之星" "收藏家")

echo "=== 灌入成就数据 ==="
echo "目标: $USER_COUNT 个用户，每人 5 个成就"
echo ""

for user_idx in $(seq 1 $USER_COUNT); do
  for ach_idx in 0 1 2 3 4; do
    type_idx=$((ach_idx % 4))

    curl -s -X POST "$BASE_URL" \
      -H "Content-Type: application/json" \
      -H "Authorization: Bearer $TOKEN" \
      -d "{
        \"user_id\": \"loadtest_user_$user_idx\",
        \"achievement_type\": \"${ACHIEVEMENT_TYPES[$type_idx]}\",
        \"title\": \"${TITLES[$ach_idx]}\",
        \"description\": \"测试成就 $ach_idx\",
        \"points\": $((10 * (ach_idx + 1)))
      }" > /dev/null
  done

  if [ $((user_idx % 20)) -eq 0 ]; then
    echo "进度: $user_idx / $USER_COUNT 用户"
  fi
done

TOTAL=$((USER_COUNT * 5))
echo ""
echo "=== 成就数据灌入完成: $TOTAL 条 ==="
