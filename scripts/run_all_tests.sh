#!/bin/bash
# 一键执行全部压测
# 用法: ./scripts/run_all_tests.sh
# 前置条件: 所有服务已启动，测试用户已创建

set -e

echo "============================================"
echo "  MicroserviceDemo 全量性能测试"
echo "  时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "============================================"
echo ""

# 检查所有服务健康状态
echo "检查服务状态..."
SERVICES=(
    "http://localhost:8090/api/v1/registry/health:service_registry"
    "http://localhost:8081/health:api_gateway"
    "http://localhost:8082/health:user_service"
    "http://localhost:8083/api/v1/auth/health:auth_service"
    "http://localhost:8084/api/v1/gamedata/health:game_data_service"
)

ALL_OK=true
for svc in "${SERVICES[@]}"; do
    URL="${svc%%:*}"
    NAME="${svc##*:}"
    CODE=$(curl -s -o /dev/null -w "%{http_code}" "$URL" 2>/dev/null || echo "000")
    if [ "$CODE" = "200" ]; then
        echo "  ✅ $NAME (HTTP $CODE)"
    else
        echo "  ❌ $NAME (HTTP $CODE)"
        ALL_OK=false
    fi
done

if [ "$ALL_OK" = false ]; then
    echo ""
    echo "ERROR: 部分服务不可用，请先启动所有服务"
    exit 1
fi
echo ""
echo "所有服务正常！"
echo ""

# 记录系统基线资源
echo "记录基线资源使用..."
echo "--- CPU ---"
top -bn1 | head -5
echo "--- 内存 ---"
free -m
echo "--- Redis ---"
redis-cli -a 123456 INFO memory 2>/dev/null | grep used_memory_human
echo "--- MySQL ---"
mysql -u root -p013ee244b29700ed -N -e "SHOW STATUS LIKE 'Threads_connected';" 2>/dev/null
echo ""

# 执行各模块压测
echo "============================================"
echo "  1/4 登录接口压测"
echo "============================================"
bash scripts/stress_login.sh
echo ""

echo "============================================"
echo "  2/4 游戏数据服务压测"
echo "============================================"
bash scripts/stress_gamedata.sh
echo ""

echo "============================================"
echo "  3/4 API Gateway 压测"
echo "============================================"
bash scripts/stress_gateway.sh
echo ""

echo "============================================"
echo "  4/4 健康检查压测"
echo "============================================"
RESULT_DIR="stress_results/health_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULT_DIR"

for svc_port in "8090:/api/v1/registry/health" "8081:/health" "8082:/health" "8083:/api/v1/auth/health" "8084:/api/v1/gamedata/health"; do
    PORT="${svc_port%%:*}"
    PATH_="${svc_port##*:}"
    NAME="port_$PORT"
    echo "--- 健康检查 :$PORT ---"
    wrk -t4 -c50 -d60s "http://localhost:$PORT$PATH_" | tee "$RESULT_DIR/$NAME.txt"
    echo ""
done

echo "============================================"
echo "  全量测试完成！"
echo "  结果目录: stress_results/"
echo "============================================"
