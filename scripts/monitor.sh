#!/bin/bash
# 系统资源监控采集脚本
# 每 5 秒采集一次指标，输出到 CSV 文件
# 用法: ./scripts/monitor.sh [采集间隔秒数，默认5]

INTERVAL=${1:-5}
OUTPUT="monitoring_$(date +%Y%m%d_%H%M%S).csv"

echo "=== 性能监控采集 ==="
echo "输出文件: $OUTPUT"
echo "采集间隔: ${INTERVAL}s"
echo "按 Ctrl+C 停止"
echo ""

# 写入 CSV 头
echo "timestamp,cpu_pct,mem_used_mb,mem_total_mb,redis_used_mem,mysql_conns,net_established" > "$OUTPUT"

while true; do
  TS=$(date '+%Y-%m-%d %H:%M:%S')

  # CPU 使用率（取整体 idle 的补数）
  CPU_IDLE=$(top -bn1 2>/dev/null | grep "Cpu(s)" | awk '{print $8}' | cut -d'%' -f1)
  if [ -z "$CPU_IDLE" ]; then
    CPU="N/A"
  else
    CPU=$(echo "100 - $CPU_IDLE" | bc 2>/dev/null || echo "N/A")
  fi

  # 内存使用
  MEM_INFO=$(free -m 2>/dev/null | awk '/Mem:/{print $3","$2}')
  if [ -z "$MEM_INFO" ]; then
    MEM_USED="N/A"
    MEM_TOTAL="N/A"
  else
    MEM_USED=$(echo "$MEM_INFO" | cut -d',' -f1)
    MEM_TOTAL=$(echo "$MEM_INFO" | cut -d',' -f2)
  fi

  # Redis 内存
  REDIS_MEM=$(redis-cli -a 123456 INFO memory 2>/dev/null | grep "used_memory_human:" | tr -d '\r' | cut -d: -f2)
  REDIS_MEM=${REDIS_MEM:-"N/A"}

  # MySQL 连接数
  MYSQL_CONNS=$(mysql -u root -p013ee244b29700ed -N -e "SHOW STATUS LIKE 'Threads_connected';" 2>/dev/null | awk '{print $2}')
  MYSQL_CONNS=${MYSQL_CONNS:-"N/A"}

  # 网络已建立连接数
  NET_EST=$(ss -tn state established 2>/dev/null | wc -l)
  NET_EST=${NET_EST:-"N/A"}

  # 写入 CSV
  echo "$TS,$CPU,$MEM_USED,$MEM_TOTAL,$REDIS_MEM,$MYSQL_CONNS,$NET_EST" >> "$OUTPUT"

  # 同时输出到终端
  echo "[$TS] CPU: ${CPU}% | Mem: ${MEM_USED}/${MEM_TOTAL}MB | Redis: ${REDIS_MEM} | MySQL: ${MYSQL_CONNS} | Conn: ${NET_EST}"

  sleep "$INTERVAL"
done
