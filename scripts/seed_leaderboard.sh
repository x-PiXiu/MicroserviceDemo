#!/bin/bash
# 批量灌入排行榜数据
# 用法: ./scripts/seed_leaderboard.sh [条目数量，默认500]

TOKEN=${TOKEN:-"YOUR_ADMIN_TOKEN"}
BASE_URL="http://localhost:8084/api/v1/gamedata/leaderboard"
ENTRY_COUNT=${1:-500}

echo "=== 灌入排行榜数据 ==="
echo "目标: $ENTRY_COUNT 条记录"
echo ""

for i in $(seq 1 $ENTRY_COUNT); do
  RATING=$((1000 + RANDOM % 2000))

  curl -s -X POST "$BASE_URL" \
    -H "Content-Type: application/json" \
    -H "Authorization: Bearer $TOKEN" \
    -d "{
      \"user_id\": \"loadtest_user_$i\",
      \"game_type\": \"gomoku\",
      \"leaderboard_type\": \"rating\",
      \"score\": $RATING
    }" > /dev/null

  if [ $((i % 100)) -eq 0 ]; then
    echo "进度: $i / $ENTRY_COUNT"
  fi
done

echo ""
echo "=== 排行榜数据灌入完成 ==="
