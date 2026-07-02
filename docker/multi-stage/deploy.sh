#!/bin/bash

# =============================================================================
# 游戏微服务 - 多阶段构建版本部署脚本
#
# 特点:
# - 每个服务独立镜像
# - 支持 BuildKit 缓存
# - 支持外部 MySQL/Redis
#
# 使用方法:
#   chmod +x docker/multi-stage/deploy.sh
#   ./docker/multi-stage/deploy.sh [选项]
#
# 选项:
#   --build          # 构建所有镜像
#   --build-service <name>  # 构建指定服务
#   --start          # 启动服务
#   --stop           # 停止服务
#   --restart         # 重启服务
#   --status         # 查看状态
#   --logs [服务名]  # 查看日志
#   --clean          # 清理所有容器和镜像
#   --fix-cache      # 修复缓存问题
#   --rebuild        # 完全重新构建
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

# 默认配置
BUILD_TYPE=Release
BUILD_JOBS=4
NO_CACHE=false
FIX_CACHE=false
PRUNE_CACHE=false
DO_DEPLOY=false

USE_EXTERNAL_DB=false
USE_EXTERNAL_REDIS=false

# 服务列表
SERVICES=(
    "service_registry"
    "user_service"
    "auth_service"
    "game_data_service"
    "gomoku_server"
)

# 显示帮助
show_help() {
    echo "游戏微服务 - 多阶段构建版本部署脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "快速部署:"
    echo "  --deploy         构建并启动所有服务（推荐日常使用）"
    echo "  --rebuild        完全重新构建（清理缓存 + 无缓存 + 启动）"
    echo ""
    echo "构建选项:"
    echo "  --build          仅构建所有服务镜像（不启动）"
    echo "  --build-service <name>  构建指定服务"
    echo "  --fix-cache      修复 BuildKit 缓存问题"
    echo ""
    echo "部署选项:"
    echo "  --start          启动所有服务"
    echo "  --stop           停止所有服务"
    echo "  --restart         重启所有服务"
    echo "  --status         查看服务状态"
    echo "  --logs [服务名]  查看日志（可选指定服务）"
    echo ""
    echo "清理选项:"
    echo "  --clean          清理所有容器和镜像"
    echo "  --prune-cache    清理构建缓存"
    echo ""
    echo "数据库选项:"
    echo "  --external-db    使用外部 MySQL"
    echo "  --external-redis 使用外部 Redis"
    echo "  --external-all   使用外部数据库和 Redis"
    echo ""
    echo "其他选项:"
    echo "  --no-cache       不使用缓存构建"
    echo "  --help, -h       显示帮助"
    echo ""
    echo "示例:"
    echo "  $0 --deploy                    # 构建并启动（使用内部数据库）"
    echo "  $0 --external-all --deploy     # 构建并启动（使用外部数据库）"
    echo "  $0 --rebuild                   # 完全重新构建"
    echo "  $0 --external-all --rebuild    # 使用外部数据库完全重建"
    echo "  $0 --build-service user_service # 仅构建用户服务"
    echo "  $0 --fix-cache --build         # 修复缓存后构建"
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

    local THIRD_PARTY_DIR="${DOCKER_DIR}/shared/third_party"

    if [ ! -d "${THIRD_PARTY_DIR}/jwt-cpp" ]; then
        log_error "jwt-cpp 不存在: 请先下载"
        echo "  运行: git clone https://github.com/Thalhammer/jwt-cpp.git ${THIRD_PARTY_DIR}/jwt-cpp"
        exit 1
    fi

    if [ ! -d "${THIRD_PARTY_DIR}/libbcrypt" ]; then
        log_error "libbcrypt 不存在, 请先下载"
        echo "  运行: git clone https://github.com/trusch/libbcrypt.git ${THIRD_PARTY_DIR}/libbcrypt"
        exit 1
    fi

    log_success "第三方依赖检查通过"
}

# 修复缓存问题
fix_cache() {
    log_step "修复 BuildKit 缓存问题..."

    # 清理 buildx 缓存
    docker buildx prune -af 2>/dev/null || docker builder prune -af

    # 清理悬空镜像
    docker image prune -af

    # 清理本地构建目录
    rm -rf "${PROJECT_ROOT}/build" 2>/dev/null || true

    log_success "缓存修复完成"
}

# 构建所有服务镜像
build_all() {
    log_step "构建所有服务镜像..."

    local cache_args=""
    if [ "$NO_CACHE" = true ]; then
        cache_args="--no-cache"
    fi

    cd "${PROJECT_ROOT}"

    for service in "${SERVICES[@]}"; do
        log_info "构建服务: ${service}"
        # 转换服务名到镜像名（下划线转连字符）
        local image_name="${service//_/-}"
        docker build \
            ${cache_args} \
            -f docker/multi-stage/Dockerfile \
            -t "${image_name}:latest" \
            --target "${service}" \
            .
    done

    log_success "所有镜像构建完成"
}

# 构建指定服务
build_service() {
    local service=$1
    log_step "构建服务: ${service}..."

    local cache_args=""
    if [ "$NO_CACHE" = true ]; then
        cache_args="--no-cache"
    fi

    cd "${PROJECT_ROOT}"

    docker build \
        ${cache_args} \
        -f docker/multi-stage/Dockerfile \
        -t "game-${service//_/-}:latest" \
        --target "${service}" \
        .

    log_success "服务镜像构建完成: ${service}"
}

# 启动服务
start_services() {
    log_step "启动服务..."

    # 检查 .env 文件
    if [ ! -f "${DOCKER_DIR}/.env" ]; then
        log_warning ".env 文件不存在，从 .env.example 复制..."
        cp "${DOCKER_DIR}/shared/.env.example" "${DOCKER_DIR}/.env"
    fi

    local profile_args=""
    if [ "$USE_EXTERNAL_DB" = false ]; then
        profile_args="${profile_args} --profile internal-db"
    fi
    if [ "$USE_EXTERNAL_REDIS" = false ]; then
        profile_args="${profile_args} --profile internal-redis"
    fi

    cd "${PROJECT_ROOT}"

    $COMPOSE_CMD --env-file "${DOCKER_DIR}/.env" -f docker/multi-stage/docker-compose.yml ${profile_args} up -d

    log_success "服务启动完成"
}

# 停止服务
stop_services() {
    log_info "停止服务..."

    cd "${PROJECT_ROOT}"

    $COMPOSE_CMD -f docker/multi-stage/docker-compose.yml down

    log_success "服务已停止"
}

# 查看状态
show_status() {
    cd "${PROJECT_ROOT}"
    $COMPOSE_CMD -f docker/multi-stage/docker-compose.yml ps
}

# 查看日志
show_logs() {
    local service=${1:-}
    cd "${PROJECT_ROOT}"
    $COMPOSE_CMD -f docker/multi-stage/docker-compose.yml logs -f ${service}
}

# 清理
clean() {
    log_warning "清理所有容器和镜像..."

    cd "${PROJECT_ROOT}"

    $COMPOSE_CMD -f docker/multi-stage/docker-compose.yml down -v --rmi
    docker system prune -af

    log_success "清理完成"
}

# 清理构建缓存
prune_cache() {
    log_info "清理构建缓存..."

    docker buildx prune -af 2>/dev/null || docker builder prune -af
    docker system prune -af

    log_success "构建缓存清理完成"
}

# 解析参数
parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --build)
                check_dependencies
                check_third_party
                build_all
                exit 0
                ;;
            --deploy)
                DO_DEPLOY=true
                shift
                ;;
            --build-service)
                if [ -z "$2" ]; then
                    log_error "请指定服务名称"
                    exit 1
                fi
                check_dependencies
                check_third_party
                build_service "$2"
                exit 0
                ;;
            --start)
                check_dependencies
                start_services
                show_status
                exit 0
                ;;
            --stop)
                stop_services
                exit 0
                ;;
            --restart)
                stop_services
                start_services
                show_status
                exit 0
                ;;
            --status)
                show_status
                exit 0
                ;;
            --logs)
                show_logs "${2:-}"
                exit 0
                ;;
            --clean)
                clean
                exit 0
                ;;
            --fix-cache)
                FIX_CACHE=true
                shift
                ;;
            --deploy)
                DO_DEPLOY=true
                shift
                ;;
            --rebuild)
                NO_CACHE=true
                PRUNE_CACHE=true
                DO_DEPLOY=true
                shift
                ;;
            --prune-cache)
                prune_cache
                exit 0
                ;;
            --no-cache)
                NO_CACHE=true
                shift
                ;;
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
            --help|-h)
                show_help
                exit 0
                ;;
            *)
                shift
                ;;
        esac
    done

}

# 主函数
main() {
    echo -e "${CYAN}"
    echo "=============================================================================="
    echo "                    游戏微服务 - 多阶段构建版本"
    echo "=============================================================================="
    echo -e "${NC}"
    echo "项目根目录: ${PROJECT_ROOT}"
    echo ""

    # 如果没有参数，显示帮助
    if [ $# -eq 0 ]; then
        show_help
        exit 0
    fi

    parse_args "$@"

    # 如果指定了修复缓存，先修复
    if [ "$FIX_CACHE" = true ]; then
        fix_cache
    fi

    # 如果指定了清理缓存
    if [ "$PRUNE_CACHE" = true ]; then
        prune_cache
    fi

    # 如果有部署标志，执行构建+启动（不清理缓存）
    if [ "$DO_DEPLOY" = true ]; then
        check_dependencies
        check_third_party
        build_all
        start_services
        show_status
    fi

    # 如果有重建标志，执行完整重建
    if [ "$NO_CACHE" = true ] && [ "$PRUNE_CACHE" = true ]; then
        check_dependencies
        check_third_party
        build_all
        start_services
        show_status
    fi
}

# 执行主函数
main "$@"
