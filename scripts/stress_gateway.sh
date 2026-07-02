#!/bin/bash
# API Gateway 压测脚本
# 用法: ./scripts/stress_gateway.sh
# 前置条件: API Gateway(:8081) 和所有后端服务已启动

set -e

GATEWAY_URL="http://localhost:8081"
RESULT_DIR="stress_results/gateway_$(date +%Y%m%d_%H%M%S)"

mkdir -p "$RESULT_DIR"

echo "=========================================="
echo "  API Gateway 压测"
echo "  目标: $GATEWAY_URL"
echo "  结果目录: $RESULT_DIR"
echo "=========================================="
echo ""

# 获取测试 Token（通过 Gateway 转发）
echo "获取测试 Token..."
TOKEN=$(curl -s -X POST "$GATEWAY_URL/api/v1/auth/login" \
    -H "Content-Type: application/json" \
    -d '{"username":"cdp","password":"Cdp123"}' | grep -o '"access_token":"[^"]*"' | cut -d'"' -f4)

if [ -z "$TOKEN" ]; then
    echo "WARN: 无法通过 Gateway 获取 Token，尝试直连 auth_service..."
    TOKEN=$(curl -s -X POST http://localhost:8083/api/v1/auth/login \
        -H "Content-Type: application/json" \
        -d '{"username":"cdp","password":"Cdp123"}' | grep -o '"access_token":"[^"]*"' | cut -d'"' -f4)
fi
echo "✅ Token: ${TOKEN:0:20}..."
echo ""

# 场景 1：健康检查
echo "--- 场景1: Gateway 健康检查 (100并发, 60s) ---"
wrk -t4 -c100 -d60s "$GATEWAY_URL/health" | tee "$RESULT_DIR/s01_health.txt"
echo ""

# 场景 2：登录转发
echo "--- 场景2: 登录转发 (50并发, 120s) ---"
wrk -t4 -c50 -d120s -s scripts/login.lua "$GATEWAY_URL/api/v1/auth/login" | tee "$RESULT_DIR/s02_login.txt"
echo ""

# 场景 3：用户信息转发
echo "--- 场景3: 用户信息 (50并发, 120s) ---"
wrk -t4 -c50 -d120s \
    -H "Authorization: Bearer $TOKEN" \
    "$GATEWAY_URL/api/v1/user/cdp" | tee "$RESULT_DIR/s03_user.txt"
echo ""

# 场景 4：游戏档案转发
echo "--- 场景4: 游戏档案 (50并发, 120s) ---"
wrk -t4 -c50 -d120s \
    -H "Authorization: Bearer $TOKEN" \
    "$GATEWAY_URL/api/v1/gamedata/profiles/cdp" | tee "$RESULT_DIR/s04_profile.txt"
echo ""

# 场景 5：排行榜转发
echo "--- 场景5: 排行榜 (100并发, 300s) ---"
wrk -t4 -c100 -d300s \
    -H "Authorization: Bearer $TOKEN" \
    "$GATEWAY_URL/api/v1/gamedata/leaderboard/rating/gomoku" | tee "$RESULT_DIR/s05_leaderboard.txt"
echo ""

echo "=========================================="
echo "  Gateway 压测完成！"
echo "  结果保存在: $RESULT_DIR"
echo "=========================================="
