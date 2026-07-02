#!/bin/bash
# 登录接口完整压测脚本
# 用法: ./scripts/stress_login.sh
# 前置条件: auth_service(:8083) 和 user_service(:8082) 已启动，测试用户已创建

set -e

AUTH_URL="http://localhost:8083/api/v1/auth/login"
LOGIN_SCRIPT="scripts/login.lua"
RESULT_DIR="stress_results/login_$(date +%Y%m%d_%H%M%S)"

mkdir -p "$RESULT_DIR"

echo "=========================================="
echo "  登录接口压测"
echo "  目标: $AUTH_URL"
echo "  结果目录: $RESULT_DIR"
echo "=========================================="
echo ""

# 检查服务是否可用
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://localhost:8083/api/v1/auth/health)
if [ "$HTTP_CODE" != "200" ]; then
    echo "ERROR: auth_service 不可用 (HTTP $HTTP_CODE)"
    exit 1
fi
echo "✅ auth_service 健康检查通过"

# 预热（10 秒，低并发）
echo ""
echo "--- 预热 (10s, 10 并发) ---"
wrk -t2 -c10 -d10s -s "$LOGIN_SCRIPT" "$AUTH_URL" > "$RESULT_DIR/warmup.txt" 2>&1
cat "$RESULT_DIR/warmup.txt"
echo ""

# 场景 1：基准测试（单用户）
echo "--- 场景1: 基准测试 (1并发, 60s) ---"
wrk -t1 -c1 -d60s -s "$LOGIN_SCRIPT" "$AUTH_URL" | tee "$RESULT_DIR/s01_baseline.txt"
echo ""

# 场景 2：低并发
echo "--- 场景2: 低并发 (10并发, 120s) ---"
wrk -t2 -c10 -d120s -s "$LOGIN_SCRIPT" "$AUTH_URL" | tee "$RESULT_DIR/s02_low.txt"
echo ""

# 场景 3：中并发
echo "--- 场景3: 中并发 (50并发, 300s) ---"
wrk -t4 -c50 -d300s -s "$LOGIN_SCRIPT" "$AUTH_URL" | tee "$RESULT_DIR/s03_medium.txt"
echo ""

# 场景 4：高并发
echo "--- 场景4: 高并发 (200并发, 300s) ---"
wrk -t8 -c200 -d300s -s "$LOGIN_SCRIPT" "$AUTH_URL" | tee "$RESULT_DIR/s04_high.txt"
echo ""

# 场景 5：峰值测试
echo "--- 场景5: 峰值测试 (500并发, 120s) ---"
wrk -t8 -c500 -d120s -s "$LOGIN_SCRIPT" "$AUTH_URL" | tee "$RESULT_DIR/s05_peak.txt"
echo ""

echo "=========================================="
echo "  登录压测完成！结果保存在: $RESULT_DIR"
echo "=========================================="
