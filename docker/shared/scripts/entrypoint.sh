#!/bin/bash
# =============================================================================
# 容器入口脚本 - 处理配置文件中的环境变量替换
#
# 说明:
#   由于 C++ yaml-cpp 库不会自动替换 ${VAR:-default} 语法，
#   本脚本使用 envsubst 在启动前处理配置文件。
#
#   envsubst 不支持 ${VAR:-default} 语法，所以需要先设置默认值。
# =============================================================================

set -e

# 配置文件目录
CONFIG_DIR="${CONFIG_DIR:-/app/config}"
TEMPLATE_DIR="${TEMPLATE_DIR:-/app/config}"

echo "[entrypoint] Initializing container..."
echo "[entrypoint] Service: ${SERVICE_NAME:-unknown}"
echo "[entrypoint] Config directory: ${CONFIG_DIR}"

# =============================================================================
# 设置默认值 - 这些变量将在 envsubst 替换时使用
# =============================================================================

# 全局配置
export TZ="${TZ:-Asia/Shanghai}"
export LOG_LEVEL="${LOG_LEVEL:-INFO}"
export ENVIRONMENT="${ENVIRONMENT:-development}"
export SERVICE_MODE="${SERVICE_MODE:-simple}"

# Redis 配置
export REDIS_HOST="${REDIS_HOST:-redis}"
export REDIS_EXTERNAL_PORT="${REDIS_EXTERNAL_PORT:-6379}"
export REDIS_PASSWORD="${REDIS_PASSWORD:-}"
export REDIS_DATABASE="${REDIS_DATABASE:-0}"
export REDIS_POOL_SIZE="${REDIS_POOL_SIZE:-10}"

# MySQL 配置
export MYSQL_HOST="${MYSQL_HOST:-mysql}"
export MYSQL_EXTERNAL_PORT="${MYSQL_EXTERNAL_PORT:-3306}"
export MYSQL_USER="${MYSQL_USER:-root}"
export MYSQL_ROOT_PASSWORD="${MYSQL_ROOT_PASSWORD:-game2025secret}"
export MYSQL_DATABASE="${MYSQL_DATABASE:-game_platform}"
export MYSQL_POOL_SIZE="${MYSQL_POOL_SIZE:-20}"

# Kafka 配置
export USE_KAFKA="${USE_KAFKA:-false}"
export KAFKA_HOST="${KAFKA_HOST:-kafka}"
export KAFKA_PORT="${KAFKA_PORT:-9092}"

# 服务注册中心配置
export SERVICE_REGISTRY_HOST="${SERVICE_REGISTRY_HOST:-service-registry}"
export SERVICE_REGISTRY_INTERNAL_PORT="${SERVICE_REGISTRY_INTERNAL_PORT:-8090}"

# 用户服务配置
export USER_SERVICE_HOST="${USER_SERVICE_HOST:-user-service}"
export USER_SERVICE_INTERNAL_PORT="${USER_SERVICE_INTERNAL_PORT:-8082}"

# 认证服务配置
export AUTH_SERVICE_HOST="${AUTH_SERVICE_HOST:-auth-service}"
export AUTH_SERVICE_INTERNAL_PORT="${AUTH_SERVICE_INTERNAL_PORT:-8083}"

# 游戏数据服务配置
export GAME_DATA_SERVICE_HOST="${GAME_DATA_SERVICE_HOST:-game-data-service}"
export GAME_DATA_SERVICE_INTERNAL_PORT="${GAME_DATA_SERVICE_INTERNAL_PORT:-8084}"

# 五子棋服务配置
export GOMOKU_SERVICE_HOST="${GOMOKU_SERVICE_HOST:-gomoku-server}"
export GOMOKU_HTTP_INTERNAL_PORT="${GOMOKU_HTTP_INTERNAL_PORT:-8085}"
export GOMOKU_WS_INTERNAL_PORT="${GOMOKU_WS_INTERNAL_PORT:-8086}"

# JWT 配置
export JWT_SECRET_KEY="${JWT_SECRET_KEY:-game_microservices_jwt_secret_key_2025_very_secure}"
export JWT_ACCESS_TOKEN_EXPIRE="${JWT_ACCESS_TOKEN_EXPIRE:-3600}"
export JWT_REFRESH_TOKEN_EXPIRE="${JWT_REFRESH_TOKEN_EXPIRE:-604800}"

# 会话加密配置
export SESSION_ENCRYPTION_KEY="${SESSION_ENCRYPTION_KEY:-session_encryption_key_2025_very_secure}"

# CORS 配置
export ENABLE_CORS="${ENABLE_CORS:-true}"
export CORS_ALLOWED_ORIGINS="${CORS_ALLOWED_ORIGINS:-*}"

# 速率限制配置
export ENABLE_RATE_LIMIT="${ENABLE_RATE_LIMIT:-true}"
export RATE_LIMIT_REQUESTS_PER_MINUTE="${RATE_LIMIT_REQUESTS_PER_MINUTE:-1000}"

# 线程配置
export WORKER_THREADS="${WORKER_THREADS:-4}"
export IO_THREADS="${IO_THREADS:-2}"

# SMTP 配置
export SMTP_HOST="${SMTP_HOST:-smtp.gmail.com}"
export SMTP_PORT="${SMTP_PORT:-587}"
export SMTP_USER="${SMTP_USER:-}"
export SMTP_PASSWORD="${SMTP_PASSWORD:-}"

# OAuth 配置
export GOOGLE_CLIENT_ID="${GOOGLE_CLIENT_ID:-}"
export GOOGLE_CLIENT_SECRET="${GOOGLE_CLIENT_SECRET:-}"
export GITHUB_CLIENT_ID="${GITHUB_CLIENT_ID:-}"
export GITHUB_CLIENT_SECRET="${GITHUB_CLIENT_SECRET:-}"
export WECHAT_APP_ID="${WECHAT_APP_ID:-}"
export WECHAT_APP_SECRET="${WECHAT_APP_SECRET:-}"
export STEAM_APP_ID="${STEAM_APP_ID:-}"
export STEAM_API_KEY="${STEAM_API_KEY:-}"
export QQ_APP_ID="${QQ_APP_ID:-}"
export QQ_APP_KEY="${QQ_APP_KEY:-}"

# =============================================================================
# 输出关键配置信息（调试用）
# =============================================================================
echo "[entrypoint] Configuration:"
echo "  - Redis: ${REDIS_HOST}:${REDIS_EXTERNAL_PORT}"
echo "  - MySQL: ${MYSQL_HOST}:${MYSQL_EXTERNAL_PORT}"
echo "  - Service Registry: ${SERVICE_REGISTRY_HOST}:${SERVICE_REGISTRY_INTERNAL_PORT}"
echo "  - Environment: ${ENVIRONMENT}"

# =============================================================================
# 处理配置文件的环境变量替换
# =============================================================================

# 定义需要替换的变量列表（用于 envsubst）
# 单引号中的 $ 是字面值，envsubst 需要这种格式
VARIABLES='$TZ $LOG_LEVEL $ENVIRONMENT $SERVICE_MODE $REDIS_HOST $REDIS_EXTERNAL_PORT $REDIS_PASSWORD $REDIS_DATABASE $REDIS_POOL_SIZE $MYSQL_HOST $MYSQL_EXTERNAL_PORT $MYSQL_USER $MYSQL_ROOT_PASSWORD $MYSQL_DATABASE $MYSQL_POOL_SIZE $USE_KAFKA $KAFKA_HOST $KAFKA_PORT $SERVICE_REGISTRY_HOST $SERVICE_REGISTRY_INTERNAL_PORT $USER_SERVICE_HOST $USER_SERVICE_INTERNAL_PORT $AUTH_SERVICE_HOST $AUTH_SERVICE_INTERNAL_PORT $GAME_DATA_SERVICE_HOST $GAME_DATA_SERVICE_INTERNAL_PORT $GOMOKU_SERVICE_HOST $GOMOKU_HTTP_INTERNAL_PORT $GOMOKU_WS_INTERNAL_PORT $JWT_SECRET_KEY $JWT_ACCESS_TOKEN_EXPIRE $JWT_REFRESH_TOKEN_EXPIRE $SESSION_ENCRYPTION_KEY $ENABLE_CORS $CORS_ALLOWED_ORIGINS $ENABLE_RATE_LIMIT $RATE_LIMIT_REQUESTS_PER_MINUTE $WORKER_THREADS $IO_THREADS $SMTP_HOST $SMTP_PORT $SMTP_USER $SMTP_PASSWORD $GOOGLE_CLIENT_ID $GOOGLE_CLIENT_SECRET $GITHUB_CLIENT_ID $GITHUB_CLIENT_SECRET $WECHAT_APP_ID $WECHAT_APP_SECRET $STEAM_APP_ID $STEAM_API_KEY $QQ_APP_ID $QQ_APP_KEY'

process_config() {
    local config_file=$1
    local source_file="${CONFIG_DIR}/${config_file}"
    local temp_file="${CONFIG_DIR}/${config_file}.tmp"

    if [ -f "$source_file" ]; then
        # 使用 envsubst 替换环境变量
        # 指定变量列表以避免误替换
        envsubst "$VARIABLES" < "$source_file" > "$temp_file"
        mv "$temp_file" "$source_file"
        echo "[entrypoint] Processed config: ${config_file}"
    fi
}

# 根据服务名处理对应的配置文件
SERVICE_NAME="${SERVICE_NAME:-}"
case "$SERVICE_NAME" in
    service_registry)
        process_config "service_registry.yml"
        ;;
    user_service)
        process_config "user_service.yml"
        ;;
    auth_service)
        process_config "auth_service_config.yml"
        ;;
    game_data_service)
        process_config "game_data_service.yml"
        ;;
    gomoku_server)
        process_config "gomoku_server.yml"
        ;;
    *)
        # 如果没有指定服务名，尝试处理所有配置文件
        echo "[entrypoint] No specific service name, processing all configs..."
        for config in service_registry.yml user_service.yml auth_service_config.yml game_data_service.yml gomoku_server.yml; do
            if [ -f "${CONFIG_DIR}/${config}" ]; then
                process_config "$config"
            fi
        done
        ;;
esac

# =============================================================================
# 执行传入的命令
# =============================================================================
echo "[entrypoint] Starting application: $@"
exec "$@"
