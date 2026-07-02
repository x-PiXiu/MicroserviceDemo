#!/bin/bash

# =============================================================================
# 游戏微服务 - 单镜像版本部署脚本
#
# 特点:
# - 使用单个镜像启动多个容器
# - 适合开发环境和快速部署
# - 避免 BuildKit 缓存问题
#
# 使用方法:
#   chmod +x docker/single-image/deploy.sh
#   ./docker/single-image/deploy.sh [选项]
#
# 选项:
#   --build          # 构建镜像
#   --rebuild        # 完全重新构建（清理缓存 + 无缓存构建）
#   --start          # 启动服务
#   --stop           # 停止服务
#   --status         # 查看状态
#   --logs [服务名]  # 查看日志
#   --clean          # 清理所有容器和镜像
#   --prune-cache    # 清理构建缓存
#   --help           # 显示帮助
# =============================================================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# 日志函数
log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1"; }

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKER_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT_ROOT="$(cd "${DOCKER_DIR}/.." && pwd)"

# 配置文件路径
SINGLE_IMAGE_DIR="${DOCKER_DIR}/single-image"
SHARED_DIR="${DOCKER_DIR}/shared"

# 默认配置
NO_CACHE=false
USE_EXTERNAL_DB=false
USE_EXTERNAL_REDIS=false

# 清理构建缓存
prune_cache() {
    log_step "清理 Docker 构建缓存..."

    # 清理 Docker 构建缓存
    docker builder prune -af 2>/dev/null || true

    # 清理悬空镜像
    docker image prune -af 2>/dev/null || true

    # 清理构建缓存（BuildKit）
    docker buildx prune -af 2>/dev/null || true

    log_success "构建缓存清理完成"
}

# 显示帮助
show_help() {
    echo "用法: $0 [选项]"
    echo ""
    echo "构建选项:"
    echo "  --build          构建单镜像"
    echo "  --rebuild        完全重新构建（清理缓存 + 无缓存构建）"
    echo ""
    echo "部署选项:"
    echo "  --start          启动所有服务"
    echo "  --stop           停止所有服务"
    echo "  --status         查看服务状态"
    echo "  --logs [服务名]  查看日志"
    echo ""
    echo "数据库选项:"
    echo "  --external-db    使用外部 MySQL（需配置 MYSQL_HOST 等）"
    echo "  --external-redis 使用外部 Redis（需配置 REDIS_HOST 等）"
    echo "  --external-all   使用外部 MySQL 和 Redis"
    echo ""
    echo "清理选项:"
    echo "  --clean          清理所有容器和镜像"
    echo "  --prune-cache    清理构建缓存"
    echo ""
    echo "其他选项:"
    echo "  --help           显示帮助信息"
    echo ""
    echo "服务名: service_registry, user_service, auth_service, game_data_service, gomoku_server"
    echo ""
    echo "示例:"
    echo "  $0 --build                # 构建镜像"
    echo "  $0 --rebuild              # 完全重新构建（推荐）"
    echo "  $0 --start                # 启动所有服务（使用内部数据库）"
    echo "  $0 --external-all --start # 使用外部数据库启动"
    echo "  $0 --logs user_service    # 查看用户服务日志"
}

# 检查依赖
check_dependencies() {
    log_info "检查系统依赖..."

    if ! command -v docker &> /dev/null; then
        log_error "Docker 未安装"
        exit 1
    fi

    if ! command -v docker-compose &> /dev/null && ! docker compose version &> /dev/null; then
        log_error "Docker Compose 未安装"
        exit 1
    fi

    if command -v docker-compose &> /dev/null; then
        COMPOSE_CMD="docker-compose"
    else
        COMPOSE_CMD="docker compose"
    fi

    log_success "依赖检查通过"
}

# 检查第三方依赖
check_third_party() {
    log_info "检查第三方依赖..."

    local THIRD_PARTY_DIR="${SHARED_DIR}/third_party"

    if [ ! -d "${THIRD_PARTY_DIR}/jwt-cpp" ]; then
        log_info "下载 jwt-cpp..."
        git clone --depth 1 https://github.com/Thalhammer/jwt-cpp.git "${THIRD_PARTY_DIR}/jwt-cpp" || {
            log_error "jwt-cpp 下载失败"
            exit 1
        }
    fi

    if [ ! -d "${THIRD_PARTY_DIR}/libbcrypt" ]; then
        log_info "下载 libbcrypt..."
        git clone --depth 1 https://github.com/trusch/libbcrypt.git "${THIRD_PARTY_DIR}/libbcrypt" || {
            log_error "libbcrypt 下载失败"
            exit 1
        }
    fi

    log_success "第三方依赖检查完成"
}

# 构建镜像
build_image() {
    log_step "构建单镜像..."

    cd "${PROJECT_ROOT}"

    local cache_flag=""
    if [ "$NO_CACHE" = true ]; then
        log_info "使用 --no-cache 模式构建..."
        cache_flag="--no-cache"
    fi

    docker build \
        ${cache_flag} \
        -f "${SINGLE_IMAGE_DIR}/Dockerfile" \
        -t game-microservices:latest \
        .

    if [ $? -eq 0 ]; then
        log_success "镜像构建完成"
    else
        log_error "镜像构建失败"
        exit 1
    fi
}

# 启动服务
start_services() {
    log_step "启动服务..."

    # 检查 .env 文件
    if [ ! -f "${DOCKER_DIR}/.env" ]; then
        log_warning ".env 文件不存在，从 .env.example 复制..."
        cp "${DOCKER_DIR}/shared/.env.example" "${DOCKER_DIR}/.env"
    fi

    # 构建 profile 参数
    local profile_args=""
    if [ "$USE_EXTERNAL_DB" = false ]; then
        log_info "使用内部 MySQL..."
        profile_args="${profile_args} --profile internal-db"
    else
        log_info "使用外部 MySQL: ${MYSQL_HOST:-未配置}"
    fi

    if [ "$USE_EXTERNAL_REDIS" = false ]; then
        log_info "使用内部 Redis..."
        profile_args="${profile_args} --profile internal-redis"
    else
        log_info "使用外部 Redis: ${REDIS_HOST:-未配置}"
    fi

    cd "${PROJECT_ROOT}"

    $COMPOSE_CMD --env-file "${DOCKER_DIR}/.env" -f "${SINGLE_IMAGE_DIR}/docker-compose.yml" ${profile_args} up -d

    log_success "服务启动完成"
}

# 停止服务
stop_services() {
    log_info "停止服务..."

    cd "${PROJECT_ROOT}"

    $COMPOSE_CMD -f "${SINGLE_IMAGE_DIR}/docker-compose.yml" down

    log_success "服务已停止"
}

# 查看状态
show_status() {
    cd "${PROJECT_ROOT}"
    $COMPOSE_CMD -f "${SINGLE_IMAGE_DIR}/docker-compose.yml" ps
}

# 查看日志
show_logs() {
    cd "${PROJECT_ROOT}"
    $COMPOSE_CMD -f "${SINGLE_IMAGE_DIR}/docker-compose.yml" logs -f "${1:-}"
}

# 清理
clean() {
    log_warning "清理所有容器和镜像..."

    cd "${PROJECT_ROOT}"

    $COMPOSE_CMD -f "${SINGLE_IMAGE_DIR}/docker-compose.yml" down -v --rmi
    docker system prune -af

    log_success "清理完成"
}

# 主函数
main() {
    # 如果没有参数，显示帮助
    if [ $# -eq 0 ]; then
        show_help
        exit 0
    fi

    # 解析参数
    local action=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --external-db)
                USE_EXTERNAL_DB=true
                shift
                ;;
            --external-redis)
                USE_EXTERNAL_REDIS=true
                shift
                ;;
            --external-all)
                USE_EXTERNAL_DB=true
                USE_EXTERNAL_REDIS=true
                shift
                ;;
            --build)
                action="build"
                shift
                ;;
            --rebuild)
                action="rebuild"
                shift
                ;;
            --start)
                action="start"
                shift
                ;;
            --stop)
                action="stop"
                shift
                ;;
            --status)
                action="status"
                shift
                ;;
            --logs)
                action="logs"
                shift
                ;;
            --clean)
                action="clean"
                shift
                ;;
            --prune-cache)
                action="prune-cache"
                shift
                ;;
            --help|-h)
                show_help
                exit 0
                ;;
            *)
                shift
                ;;
        esac
    done

    # 执行操作
    case "$action" in
        build)
            check_dependencies
            check_third_party
            build_image
            ;;
        rebuild)
            check_dependencies
            check_third_party
            prune_cache
            NO_CACHE=true build_image
            ;;
        start)
            check_dependencies
            start_services
            ;;
        stop)
            stop_services
            ;;
        status)
            show_status
            ;;
        logs)
            show_logs "${2:-}"
            ;;
        clean)
            clean
            ;;
        prune-cache)
            prune_cache
            ;;
        "")
            show_help
            ;;
    esac
}

# 执行主函数
main "$@"
