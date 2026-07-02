#!/bin/bash
# 游戏数据服务完整压测脚本
# 用法: ./scripts/stress_gamedata.sh
# 前置条件: game_data_service(:8084) 已启动，测试数据已灌入

set -e

GAME_DATA_URL="http://localhost:8084"
RESULT_DIR="stress_results/gamedata_$(date +%Y%m%d_%H%M%S)"

mkdir -p "$RESULT_DIR"

echo "=========================================="
echo "  游戏数据服务压测"
echo "  目标: $GAME_DATA_URL"
echo "  结果目录: $RESULT_DIR"
echo "=========================================="
echo ""

# 获取测试 Token
echo "获取测试 Token..."
TOKEN=$(curl -s -X POST http://localhost:8083/api/v1/auth/login \
    -H "Content-Type: application/json" \
    -d '{"username":"cdp","password":"Cdp123"}' | grep -o '"access_token":"[^"]*"' | cut -d'"' -f4)

if [ -z "$TOKEN" ]; then
    echo "ERROR: 无法获取 Token"
    exit 1
fi
echo "✅ Token 获取成功"
echo ""

# 预热缓存
echo "预热缓存..."
curl -s -H "Authorization: Bearer $TOKEN" "$GAME_DATA_URL/api/v1/gamedata/profiles/cdp" > /dev/null
curl -s -H "Authorization: Bearer $TOKEN" "$GAME_DATA_URL/api/v1/gamedata/achievements/cdp" > /dev/null
curl -s -H "Authorization: Bearer $TOKEN" "$GAME_DATA_URL/api/v1/gamedata/leaderboard/rating/gomoku" > /dev/null
curl -s -H "Authorization: Bearer $TOKEN" "$GAME_DATA_URL/api/v1/gamedata/currency/cdp" > /dev/null
echo "✅ 缓存预热完成"
echo ""

# 场景 1：游戏档案（缓存命中）
echo "--- 场景1: 游戏档案 (50并发, 120s, 缓存命中) ---"
wrk -t4 -c50 -d120s \
    -H "Authorization: Bearer $TOKEN" \
    "$GAME_DATA_URL/api/v1/gamedata/profiles/cdp" | tee "$RESULT_DIR/s01_profile_cache.txt"
echo ""

# 场景 2：成就列表
echo "--- 场景2: 成就列表 (50并发, 120s) ---"
wrk -t4 -c50 -d120s \
    -H "Authorization: Bearer $TOKEN" \
    "$GAME_DATA_URL/api/v1/gamedata/achievements/cdp" | tee "$RESULT_DIR/s02_achievements.txt"
echo ""

# 场景 3：排行榜
echo "--- 场景3: 排行榜 (100并发, 300s) ---"
wrk -t4 -c100 -d300s \
    -H "Authorization: Bearer $TOKEN" \
    "$GAME_DATA_URL/api/v1/gamedata/leaderboard/rating/gomoku" | tee "$RESULT_DIR/s03_leaderboard.txt"
echo ""

# 场景 4：排行榜（带分页）
echo "--- 场景4: 排行榜分页 (100并发, 120s, limit=50) ---"
wrk -t4 -c100 -d120s \
    -H "Authorization: Bearer $TOKEN" \
    "$GAME_DATA_URL/api/v1/gamedata/leaderboard/rating/gomoku?limit=50&offset=0" | tee "$RESULT_DIR/s04_leaderboard_paged.txt"
echo ""

# 场景 5：货币查询
echo "--- 场景5: 货币查询 (50并发, 120s) ---"
wrk -t4 -c50 -d120s \
    -H "Authorization: Bearer $TOKEN" \
    "$GAME_DATA_URL/api/v1/gamedata/currency/cdp" | tee "$RESULT_DIR/s05_currency.txt"
echo ""

# 场景 6：库存查询
echo "--- 场景6: 库存查询 (50并发, 120s) ---"
wrk -t4 -c50 -d120s \
    -H "Authorization: Bearer $TOKEN" \
    "$GAME_DATA_URL/api/v1/gamedata/inventory/cdp" | tee "$RESULT_DIR/s06_inventory.txt"
echo ""

# 场景 7：健康检查
echo "--- 场景7: 健康检查 (100并发, 60s) ---"
wrk -t4 -c100 -d60s \
    "$GAME_DATA_URL/api/v1/gamedata/health" | tee "$RESULT_DIR/s07_health.txt"
echo ""

echo "=========================================="
echo "  游戏数据服务压测完成！"
echo "  结果保存在: $RESULT_DIR"
echo "=========================================="
