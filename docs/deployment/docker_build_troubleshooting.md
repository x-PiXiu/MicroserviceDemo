# Docker 构建问题排查指南

本文档提供 Docker 构建过程中常见问题的诊断和解决方案。

## 目录
- [常见问题](#常见问题)
- [缓存问题修复](#缓存问题修复)
- [网络问题处理](#网络问题处理)
- [构建失败排查](#构建失败排查)
- [最佳实践](#最佳实践)

---

## 常见问题

### 问题 1: 构建缓存损坏

**症状:**
```
ERROR: failed to solve with frontend dockerfile.v0: failed to create LLB definition
```

**原因:**
Docker BuildKit 缓存损坏或不一致。

**解决方案:**
```bash
# 方案 1: 清理 buildx 缓存
docker buildx prune -af

# 方案 2: 使用修复脚本
./docker/deploy.sh --fix-cache

# 方案 3: 完全重建
./docker/deploy.sh --rebuild
```

---

### 问题 2: --no-cache 参数不生效

**症状:**
使用 `--no-cache` 参数后仍然使用了旧缓存。

**原因:**
BuildKit 的缓存机制与传统 Docker 不同，`--no-cache` 只影响当前构建命令，不影响已存在的基础镜像层。

**解决方案:**
```bash
# 方案 1: 先清理缓存再构建
./docker/deploy.sh --fix-cache --no-cache

# 方案 2: 手动删除基础镜像
docker rmi -f game-builder:latest
docker rmi -f game-runtime:latest
./docker/deploy.sh --no-cache

# 方案 3: 使用 --rebuild 参数（清理缓存 + 无缓存构建）
./docker/deploy.sh --rebuild
```

---

### 问题 3: 第三方依赖下载失败

**症状:**
```
fatal: unable to access 'https://github.com/...': Connection timed out
```

**原因:**
国内网络访问 GitHub 不稳定。

**解决方案:**
```bash
# 方案 1: 使用镜像站
# 手动下载
git clone https://gitee.com/mirrors/jwt-cpp.git docker/third_party/jwt-cpp
git clone https://gitee.com/mirrors/libbcrypt.git docker/third_party/libbcrypt

# 方案 2: 使用代理
# 设置 Git 代理
git config --global http.proxy http://your-proxy:port

# 方案 3: 手动下载后复制
# 在网络良好的环境下载后，复制到服务器
scp -r docker/third_party user@server:/path/to/MicroserviceDemo/docker/
```

---

### 问题 4: 编译内存不足

**症状:**
```
c++: fatal error: Killed signal terminated program cc1plus
```

**原因:**
编译过程中内存不足，特别是并行编译时。

**解决方案:**
```bash
# 方案 1: 减少并行编译数
# 在 Dockerfile 中修改 BUILD_JOBS 参数
docker build --build-arg BUILD_JOBS=2 -f docker/Dockerfile ...

# 方案 2: 增加 Docker 内存限制
# 在 docker-compose.yml 中调整
deploy:
  resources:
    limits:
      memory: 2G

# 方案 3: 添加 swap 空间
sudo fallocate -l 4G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
```

---

## 缓存问题修复

### 快速修复脚本

```bash
#!/bin/bash
# 文件: docker/scripts/fix-cache.sh

echo "清理 Docker BuildKit 缓存..."

# 1. 停止所有构建
docker buildx stop 2>/dev/null || true

# 2. 清理 buildx 缓存
docker buildx prune -af 2>/dev/null || docker builder prune -af

# 3. 清理悬空镜像
docker image prune -af

# 4. 清理未使用的构建缓存
docker system prune -af

# 5. 重置 buildx builder
docker buildx use default 2>/dev/null || true
docker buildx create --use --name rebuilt 2>/dev/null || true

echo "缓存清理完成！"
```

### 手动修复步骤

```bash
# 步骤 1: 查看 buildx 状态
docker buildx ls

# 步骤 2: 清理特定 builder 的缓存
docker buildx prune --builder default

# 步骤 3: 删除并重建 builder
docker buildx rm mybuilder 2>/dev/null || true
docker buildx create --name mybuilder --use

# 步骤 4: 验证清理结果
docker buildx du --verbose
```

---

## 网络问题处理

### 配置国内镜像加速

```bash
# 创建或编辑 /etc/docker/daemon.json
{
  "registry-mirrors": [
    "https://docker.mirrors.ustc.edu.cn",
    "https://registry.docker-cn.com",
    "https://docker.m.daocloud.io"
  ],
  "insecure-registries": [],
  "debug": false,
  "experimental": false
}

# 重启 Docker
sudo systemctl restart docker
```

### 配置 Git 代理

```bash
# 临时使用代理
export https_proxy=http://your-proxy:port

# 永久配置
git config --global http.proxy http://your-proxy:port
git config --global https.proxy http://your-proxy:port

# 使用 SSH 替代 HTTPS
git config --global url."https://github.com/".insteadOf git@github.com:
```

---

## 构建失败排查

### 排查步骤

```bash
# 步骤 1: 查看详细错误
docker build --progress=plain -f docker/Dockerfile . 2>&1 | tee build.log

# 步骤 2: 检查磁盘空间
df -h

# 步骤 3: 检查 Docker 状态
docker info
docker system df

# 步骤 4: 检查容器日志
docker logs $(docker ps -ql) 2>&1

# 步骤 5: 进入失败的容器
docker run --rm -it --entrypoint /bin/bash game-builder:latest
```

### 常见编译错误

| 错误信息 | 可能原因 | 解决方案 |
|---------|---------|---------|
| `undefined reference to ...` | 链接库缺失 | 检查 CMakeLists.txt 中的 target_link_libraries |
| `fatal error: ...: No such file` | 头文件缺失 | 检查 include 目录路径 |
| `CMake Error: Could not find ...` | 依赖未安装 | 检查 apt-get install 命令 |
| `signal: Killed` | 内存不足 | 减少 BUILD_JOBS 或增加内存 |

---

## 最佳实践

### 1. 分层构建

```bash
# 推荐: 使用分层构建脚本
./docker/scripts/build.sh --base-only    # 先构建基础镜像
./docker/scripts/build.sh --services-only  # 再构建服务镜像
```

### 2. 增量构建

```bash
# 只修改了配置文件时，从 runtime-base 开始构建
docker build -f docker/Dockerfile \
  --target user_service \
  --cache-from game-runtime:latest \
  -t game-user-service:latest .
```

### 3. 并行构建

```bash
# 使用并行构建加速（多核 CPU）
./docker/deploy.sh --parallel
```

### 4. 定期清理

```bash
# 建议每周执行一次
docker system prune -af
docker buildx prune -af
```

### 5. 监控构建

```bash
# 查看构建历史
docker images --filter "dangling=false" --format "{{.Repository}}:{{.Tag}}\t{{.CreatedAt}}"

# 查看构建缓存使用情况
docker buildx du --verbose
```

---

## 快速命令参考

```bash
# 完整重建
./docker/deploy.sh --rebuild

# 修复缓存
./docker/deploy.sh --fix-cache

# 仅构建镜像
./docker/deploy.sh --build-only

# 仅启动服务
./docker/deploy.sh --start-only

# 并行构建
./docker/deploy.sh --parallel

# 查看日志
./docker/deploy.sh --logs user_service

# 查看状态
./docker/deploy.sh --status

# 停止服务
./docker/deploy.sh --stop

# 完全清理
./docker/deploy.sh --cleanup
```
