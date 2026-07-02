# 宝塔面板部署指南 - Game Platform Client

> **本文档仅使用 HTTP（80 端口），适用于开发/测试环境。生产环境请额外配置 HTTPS。**

---

## 目录

1. [架构概述](#1-架构概述)
2. [环境准备](#2-环境准备)
3. [部署方式一：静态文件部署（推荐）](#3-部署方式一静态文件部署推荐)
4. [部署方式二：源码运行部署（PM2）](#4-部署方式二源码运行部署pm2)
5. [部署方式三：Docker 容器部署](#5-部署方式三docker-容器部署)
6. [部署方式四：宝塔 Node 项目管理](#6-部署方式四宝塔-node-项目管理)
7. [Nginx 配置详解](#7-nginx-配置详解)
8. [常见问题排查](#8-常见问题排查)

---

## 1. 架构概述

### 1.1 整体架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              用户浏览器                                  │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼ HTTP :80
┌─────────────────────────────────────────────────────────────────────────┐
│                           宿主机 Nginx (80)                              │
│                                                                         │
│   路由规则:                                                             │
│   ├── /              → 前端服务 (静态文件 或 Vite Preview :3001)         │
│   ├── /api/v1/*      → 后端 Nginx (:10080)                              │
│   ├── /ws            → 后端 Nginx (:10080)                              │
│   └── /health        → 健康检查响应                                     │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                    ┌───────────────┴───────────────┐
                    │                               │
                    ▼                               ▼
┌───────────────────────────┐       ┌───────────────────────────────────┐
│   前端服务 (可选方式)      │       │       后端 Nginx (:10080)          │
│                           │       │                                   │
│   方式1: 静态文件         │       │   /api/v1/* → Docker 微服务       │
│   方式2: PM2 :3001       │       │   /ws       → Docker WebSocket    │
│   方式3: Docker :3001    │       │                                   │
│   方式4: 宝塔 Node       │       └───────────────────────────────────┘
└───────────────────────────┘                       │
                                                    ▼
                                    ┌───────────────────────────────────┐
                                    │         Docker 容器集群            │
                                    │                                   │
                                    │   game-gateway      (:8080)       │
                                    │   game-user-service (:8081)       │
                                    │   game-room-service (:8082)       │
                                    └───────────────────────────────────┘
```

### 1.2 端口说明

| 端口 | 服务 | 对外开放 | 说明 |
|------|------|----------|------|
| 80 | 宿主机 Nginx | 是 | 唯一对外入口 |
| 3001 | Vite Preview | 否 | 仅内网访问（方式2/3/4） |
| 10080 | 后端 Nginx | 否 | Docker 映射端口 |

### 1.3 部署方式对比

| 方式 | 复杂度 | 性能 | 适用场景 | 需要组件 |
|------|--------|------|----------|----------|
| **静态文件** | 低 | 高 | 生产环境 | Nginx |
| **PM2 源码运行** | 中 | 中 | 开发/测试 | Nginx + Node.js + PM2 |
| **Docker 容器** | 中 | 中 | 容器化环境 | Nginx + Docker |
| **宝塔 Node 项目** | 低 | 中 | 快速部署 | 宝塔 PM2 管理器 |

---

## 2. 环境准备

### 2.1 安装宝塔面板（如未安装）

```bash
# CentOS 安装命令
yum install -y wget && wget -O install.sh https://download.bt.cn/install/install_6.0.sh && sh install.sh

# Ubuntu/Deepin 安装命令
wget -O install.sh https://download.bt.cn/install/install-ubuntu_6.0.sh && sudo bash install.sh
```

### 2.2 安装必要软件

在宝塔面板【软件商店】中安装：

1. **Nginx 1.24+**（必须）
   - 搜索「Nginx」→ 点击安装
   - 选择「极速安装」或「编译安装」

2. **Node.js 版本管理器**（方式2/4需要）
   - 搜索「Node.js 版本管理器」→ 安装
   - 安装后点击【设置】→ 安装 Node.js 18.x 或 20.x

3. **PM2 管理器**（方式2/4需要）
   - 搜索「PM2管理器」→ 安装
   - 或通过命令行安装：`npm install -g pm2`

4. **Docker 管理器**（方式3需要）
   - 搜索「Docker管理器」→ 安装

### 2.3 验证环境

通过 SSH 终端验证：

```bash
# 检查 Nginx
nginx -v
# 输出: nginx version: nginx/1.24.0

# 检查 Node.js（方式2/4需要）
node -v
# 输出: v18.x.x 或 v20.x.x

# 检查 PM2（方式2/4需要）
pm2 -v
# 输出: 5.x.x

# 检查 Docker（方式3需要）
docker -v
# 输出: Docker version 24.x.x
```

### 2.4 创建网站目录

```bash
# 创建项目目录
mkdir -p /www/wwwroot/game-platform-client

# 创建日志目录
mkdir -p /www/wwwlogs/game-platform-client

# 设置权限
chown -R www:www /www/wwwroot/game-platform-client
chown -R www:www /www/wwwlogs/game-platform-client
```

---

## 3. 部署方式一：静态文件部署（推荐）

> **特点**：最简单、性能最好、资源占用最低。适合生产环境。

### 3.1 步骤概览

```
本地构建 → 上传 dist → 配置 Nginx → 完成
```

### 3.2 详细步骤

#### 步骤 1：本地构建

在本地开发机器上执行：

```bash
# 1. 进入项目目录
cd /path/to/game-platform-client

# 2. 安装依赖
npm install

# 3. 配置生产环境变量
# 编辑 .env.production 文件
```

`.env.production` 内容：

```env
# API 基础路径（相对路径，由 Nginx 代理到后端）
VITE_API_BASE_URL=/api/v1

# WebSocket 基础路径（相对路径，由 Nginx 代理到后端）
VITE_WS_BASE_URL=/ws

# 应用标题
VITE_APP_TITLE=游戏平台

# 环境
NODE_ENV=production
```

```bash
# 4. 执行构建
npm run build

# 构建完成后，dist/ 目录包含所有静态文件
ls -la dist/
```

#### 步骤 2：上传到服务器

**方式 A：宝塔文件管理器上传**

1. 打开宝塔面板 → 【文件】
2. 进入 `/www/wwwroot/game-platform-client/`
3. 删除旧文件（如有）
4. 上传 `dist/` 目录内的所有文件（不是 dist 目录本身）
5. 最终结构：
   ```
   /www/wwwroot/game-platform-client/
   ├── index.html
   ├── assets/
   │   ├── index-xxx.js
   │   ├── index-xxx.css
   │   └── ...
   ├── sw.js
   ├── manifest.webmanifest
   └── pwa-xxx.png
   ```

**方式 B：使用 scp 命令上传**

```bash
# 在本地执行，上传 dist 目录内容到服务器
scp -r dist/* root@43.143.124.220:/www/wwwroot/game-platform-client/
```

**方式 C：使用 rsync 同步（推荐）**

```bash
# 增量同步，只上传变化的文件
rsync -avz --delete dist/ root@43.143.124.220:/www/wwwroot/game-platform-client/
```

#### 步骤 3：设置文件权限

```bash
# SSH 登录服务器后执行
cd /www/wwwroot/game-platform-client
chown -R www:www .
chmod -R 755 .
```

#### 步骤 4：配置 Nginx

**方式 A：通过宝塔面板配置**

1. 宝塔面板 → 【网站】→【添加站点】
2. 配置：
   - 域名：`43.143.124.220`（或你的域名）
   - 根目录：`/www/wwwroot/game-platform-client`
   - PHP版本：纯静态
   - 创建数据库：否

3. 点击站点【设置】→【配置文件】，替换为以下内容：

```nginx
# =============================================================================
# 客户端 Nginx 配置 - 静态文件部署
#
# 部署位置: /www/server/panel/vhost/nginx/43.143.124.220.conf
# =============================================================================

# 后端 API 上游服务器
upstream backend_api {
    server 127.0.0.1:10080;
    keepalive 32;
}

server {
    listen 80;
    listen [::]:80;
    server_name 43.143.124.220;  # 替换为你的域名或 IP

    # 网站根目录
    root /www/wwwroot/game-platform-client;
    index index.html;

    # 日志
    access_log /www/wwwlogs/game-platform-client.access.log;
    error_log /www/wwwlogs/game-platform-client.error.log;

    # 安全头
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;

    # Gzip 压缩
    gzip on;
    gzip_min_length 1k;
    gzip_buffers 4 16k;
    gzip_comp_level 6;
    gzip_types text/plain text/css text/javascript application/javascript application/json application/xml image/svg+xml;
    gzip_vary on;

    # =========================================================================
    # 健康检查
    # =========================================================================
    location = /health {
        access_log off;
        return 200 "OK\n";
        add_header Content-Type text/plain;
    }

    # =========================================================================
    # WebSocket 代理 → 后端
    # =========================================================================
    location /ws {
        proxy_pass http://backend_api;
        proxy_http_version 1.1;

        # WebSocket 必需头
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";

        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;

        # 长连接超时
        proxy_connect_timeout 60s;
        proxy_send_timeout 3600s;
        proxy_read_timeout 3600s;

        proxy_buffering off;
    }

    # =========================================================================
    # API 代理 → 后端
    # =========================================================================
    location /api/v1/ {
        proxy_pass http://backend_api;
        proxy_http_version 1.1;

        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Request-ID $request_id;
        proxy_set_header Connection "";

        proxy_connect_timeout 30s;
        proxy_send_timeout 60s;
        proxy_read_timeout 60s;
    }

    # =========================================================================
    # 静态资源缓存
    # =========================================================================
    location ~* \.(?:css|js)$ {
        expires 1y;
        add_header Cache-Control "public, max-age=31536000, immutable";
        access_log off;
    }

    location ~* \.(?:jpg|jpeg|gif|png|ico|svg|webp|woff2?|ttf|eot)$ {
        expires 1y;
        add_header Cache-Control "public, max-age=31536000, immutable";
        access_log off;
    }

    # =========================================================================
    # PWA 相关文件
    # =========================================================================
    location = /sw.js {
        add_header Cache-Control "no-cache, no-store, must-revalidate";
        expires 0;
    }

    location = /manifest.webmanifest {
        types {
            application/manifest+json manifest.webmanifest;
        }
        expires 1d;
    }

    # =========================================================================
    # Vue Router History 模式
    # =========================================================================
    location / {
        try_files $uri $uri/ /index.html;
    }

    # =========================================================================
    # 禁止访问隐藏文件
    # =========================================================================
    location ~ /\. {
        deny all;
        access_log off;
        log_not_found off;
    }
}
```

**方式 B：直接编辑配置文件**

```bash
# 编辑配置文件
vi /www/server/panel/vhost/nginx/43.143.124.220.conf

# 粘贴上述配置内容

# 测试配置
nginx -t

# 重载 Nginx
nginx -s reload
```

#### 步骤 5：验证部署

```bash
# 1. 检查文件
ls -la /www/wwwroot/game-platform-client/

# 2. 测试前端访问
curl -I http://43.143.124.220/
# 应返回 200 OK

# 3. 测试 API 代理
curl http://43.143.124.220/api/v1/services/health
# 应返回后端健康检查响应

# 4. 测试健康检查
curl http://43.143.124.220/health
# 应返回 OK
```

#### 步骤 6：浏览器访问

打开浏览器访问 `http://43.143.124.220`

### 3.3 更新部署

当有新版本需要部署时：

```bash
# 1. 本地重新构建
npm run build

# 2. 上传到服务器
rsync -avz --delete dist/ root@43.143.124.220:/www/wwwroot/game-platform-client/

# 3. 无需重启 Nginx（静态文件直接生效）
```

---

## 4. 部署方式二：源码运行部署（PM2）

> **特点**：可在服务器上直接运行源码，支持热更新，适合开发/测试环境。

### 4.1 步骤概览

```
上传源码 → npm install → npm run build → PM2 启动 preview → 配置 Nginx 代理
```

### 4.2 详细步骤

#### 步骤 1：上传源码到服务器

**方式 A：Git 克隆（推荐）**

```bash
# SSH 登录服务器
cd /www/wwwroot

# 克隆代码
git clone https://your-git-repo/game-platform-client.git

cd game-platform-client
```

**方式 B：上传压缩包**

```bash
# 本地打包（排除 node_modules）
# 在项目根目录执行：
tar -czvf game-platform-client.tar.gz --exclude=node_modules --exclude=dist .

# 上传到服务器
scp game-platform-client.tar.gz root@43.143.124.220:/www/wwwroot/

# 服务器解压
cd /www/wwwroot
mkdir -p game-platform-client
tar -xzvf game-platform-client.tar.gz -C game-platform-client/
```

#### 步骤 2：安装依赖

```bash
cd /www/wwwroot/game-platform-client

# 安装依赖
npm install

# 或使用国内镜像加速
npm install --registry=https://registry.npmmirror.com
```

#### 步骤 3：配置环境变量

```bash
# 编辑 .env.production
vi .env.production
```

内容：

```env
VITE_API_BASE_URL=/api/v1
VITE_WS_BASE_URL=/ws
VITE_APP_TITLE=游戏平台
NODE_ENV=production
```

#### 步骤 4：修改 Vite 配置

编辑 `vite.config.ts`：

```typescript
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],

  // 添加以下配置
  server: {
    host: '0.0.0.0',
    port: 3001
  },
  preview: {
    host: '0.0.0.0',
    port: 3001,
    strictPort: true
  }
})
```

#### 步骤 5：构建项目

```bash
cd /www/wwwroot/game-platform-client

# 构建
npm run build

# 确认 dist 目录生成
ls -la dist/
```

#### 步骤 6：创建 PM2 配置文件

```bash
vi ecosystem.config.cjs
```

内容：

```javascript
module.exports = {
  apps: [
    {
      name: 'game-platform-client',
      script: 'npm',
      args: 'run preview',
      cwd: '/www/wwwroot/game-platform-client',
      instances: 1,
      autorestart: true,
      watch: false,
      max_memory_restart: '500M',
      env: {
        NODE_ENV: 'production',
        PORT: 3001
      },
      error_file: '/www/wwwlogs/game-platform-client/error.log',
      out_file: '/www/wwwlogs/game-platform-client/out.log',
      log_date_format: 'YYYY-MM-DD HH:mm:ss',
      merge_logs: true,
      max_restarts: 10,
      restart_delay: 1000
    }
  ]
}
```

#### 步骤 7：启动 PM2 服务

```bash
# 确保日志目录存在
mkdir -p /www/wwwlogs/game-platform-client

# 启动服务
pm2 start ecosystem.config.cjs

# 查看状态
pm2 status

# 查看日志
pm2 logs game-platform-client

# 设置开机自启
pm2 save
pm2 startup
# 按照提示执行输出的命令
```

#### 步骤 8：配置 Nginx 反向代理

创建或编辑 `/www/server/panel/vhost/nginx/43.143.124.220.conf`：

```nginx
# =============================================================================
# 客户端 Nginx 配置 - PM2 源码运行
# =============================================================================

# 后端 API 上游
upstream backend_api {
    server 127.0.0.1:10080;
    keepalive 32;
}

# 前端 Vite Preview 上游
upstream frontend_server {
    server 127.0.0.1:3001;
    keepalive 16;
}

server {
    listen 80;
    listen [::]:80;
    server_name 43.143.124.220;

    # 日志
    access_log /www/wwwlogs/game-platform-client.access.log;
    error_log /www/wwwlogs/game-platform-client.error.log;

    # 安全头
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;

    # Gzip 压缩
    gzip on;
    gzip_min_length 1k;
    gzip_buffers 4 16k;
    gzip_comp_level 6;
    gzip_types text/plain text/css text/javascript application/javascript application/json application/xml image/svg+xml;
    gzip_vary on;

    # 健康检查
    location = /health {
        access_log off;
        return 200 "OK\n";
        add_header Content-Type text/plain;
    }

    # WebSocket → 后端
    location /ws {
        proxy_pass http://backend_api;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_connect_timeout 60s;
        proxy_send_timeout 3600s;
        proxy_read_timeout 3600s;
        proxy_buffering off;
    }

    # API → 后端
    location /api/v1/ {
        proxy_pass http://backend_api;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Request-ID $request_id;
        proxy_set_header Connection "";
        proxy_connect_timeout 30s;
        proxy_send_timeout 60s;
        proxy_read_timeout 60s;
    }

    # 前端 → Vite Preview (3001)
    location / {
        proxy_pass http://frontend_server;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_connect_timeout 60s;
        proxy_send_timeout 60s;
        proxy_read_timeout 60s;
        proxy_buffering off;
    }

    # 禁止访问隐藏文件
    location ~ /\. {
        deny all;
    }
}
```

```bash
# 测试并重载
nginx -t && nginx -s reload
```

#### 步骤 9：验证部署

```bash
# 检查 PM2 状态
pm2 status

# 测试前端服务
curl http://127.0.0.1:3001/

# 测试 Nginx 代理
curl http://43.143.124.220/
```

### 4.3 常用 PM2 命令

```bash
# 查看状态
pm2 status

# 查看日志
pm2 logs game-platform-client

# 重启服务
pm2 restart game-platform-client

# 停止服务
pm2 stop game-platform-client

# 删除服务
pm2 delete game-platform-client

# 监控
pm2 monit
```

### 4.4 更新部署

```bash
cd /www/wwwroot/game-platform-client

# 拉取代码
git pull

# 安装新依赖
npm install

# 重新构建
npm run build

# 重启服务
pm2 restart game-platform-client
```

---

## 5. 部署方式三：Docker 容器部署

> **特点**：环境隔离、一致性高，适合容器化环境。

### 5.1 步骤概览

```
创建 Dockerfile → 构建镜像 → 运行容器 → 配置 Nginx 代理
```

### 5.2 详细步骤

#### 步骤 1：创建 Dockerfile

在项目根目录创建 `Dockerfile`：

```dockerfile
# =============================================================================
# Game Platform Client - Dockerfile
# =============================================================================

# 构建阶段
FROM node:20-alpine AS builder

WORKDIR /app

# 复制 package.json
COPY package*.json ./

# 安装依赖
RUN npm ci --registry=https://registry.npmmirror.com

# 复制源码
COPY . .

# 构建参数（可选）
ARG VITE_API_BASE_URL=/api/v1
ARG VITE_WS_BASE_URL=/ws

# 设置环境变量
ENV VITE_API_BASE_URL=$VITE_API_BASE_URL
ENV VITE_WS_BASE_URL=$VITE_WS_BASE_URL

# 构建
RUN npm run build

# 生产阶段
FROM node:20-alpine AS production

WORKDIR /app

# 复制构建产物
COPY --from=builder /app/dist ./dist
COPY --from=builder /app/package*.json ./

# 安装 serve 或使用 vite preview
RUN npm install vite

# 暴露端口
EXPOSE 3001

# 启动命令
CMD ["npx", "vite", "preview", "--host", "0.0.0.0", "--port", "3001"]
```

#### 步骤 2：创建 .dockerignore

```
node_modules
dist
.git
.gitignore
*.md
.env.local
.env.*.local
```

#### 步骤 3：构建镜像

```bash
# 在项目根目录执行
docker build -t game-platform-client:latest .

# 或带构建参数
docker build \
  --build-arg VITE_API_BASE_URL=/api/v1 \
  --build-arg VITE_WS_BASE_URL=/ws \
  -t game-platform-client:latest .
```

#### 步骤 4：运行容器

```bash
# 运行容器
docker run -d \
  --name game-platform-client \
  --restart always \
  -p 3001:3001 \
  game-platform-client:latest

# 查看容器状态
docker ps

# 查看日志
docker logs -f game-platform-client
```

#### 步骤 5：配置 Nginx

Nginx 配置与【部署方式二】相同，前端代理到 `127.0.0.1:3001`。

### 5.3 Docker Compose 部署（可选）

创建 `docker-compose.yml`：

```yaml
version: '3.8'

services:
  game-platform-client:
    build:
      context: .
      dockerfile: Dockerfile
      args:
        VITE_API_BASE_URL: /api/v1
        VITE_WS_BASE_URL: /ws
    container_name: game-platform-client
    restart: always
    ports:
      - "3001:3001"
    networks:
      - game-network

networks:
  game-network:
    external: true  # 使用外部网络，与后端通信
```

```bash
# 启动
docker-compose up -d

# 查看日志
docker-compose logs -f

# 停止
docker-compose down
```

---

## 6. 部署方式四：宝塔 Node 项目管理

> **特点**：图形化界面操作，适合不熟悉命令行的用户。

### 6.1 详细步骤

#### 步骤 1：安装 PM2 管理器

宝塔面板 → 【软件商店】→ 搜索「PM2管理器」→ 安装

#### 步骤 2：上传项目

通过宝塔【文件】功能上传项目到 `/www/wwwroot/game-platform-client`

#### 步骤 3：配置项目

1. 宝塔面板 → 【软件商店】→【PM2管理器】→【设置】
2. 点击【添加项目】
3. 填写信息：
   - **项目名称**：`game-platform-client`
   - **启动文件**：选择 `npm`
   - **运行目录**：`/www/wwwroot/game-platform-client`
   - **启动参数**：`run preview`
   - **端口**：`3001`
   - **运行用户**：`www`

#### 步骤 4：构建并启动

通过 SSH 终端：

```bash
cd /www/wwwroot/game-platform-client
npm install
npm run build
```

然后在 PM2 管理器中启动项目。

#### 步骤 5：配置 Nginx

与【部署方式二】的 Nginx 配置相同。

---

## 7. Nginx 配置详解

### 7.1 完整配置文件（静态文件部署）

```nginx
# =============================================================================
# 客户端 Nginx 完整配置 - 静态文件部署
# 文件位置: /www/server/panel/vhost/nginx/43.143.124.220.conf
# =============================================================================

# 后端上游定义
upstream backend_api {
    server 127.0.0.1:10080;
    keepalive 32;  # 保持 32 个长连接
}

server {
    # 监听端口
    listen 80;
    listen [::]:80;  # 支持 IPv6

    # 服务器名称（域名或 IP）
    server_name 43.143.124.220;

    # 网站根目录
    root /www/wwwroot/game-platform-client;
    index index.html;

    # 访问日志
    access_log /www/wwwlogs/game-platform-client.access.log;
    error_log /www/wwwlogs/game-platform-client.error.log;

    # =========================================================================
    # 安全响应头
    # =========================================================================
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;
    add_header Referrer-Policy "strict-origin-when-cross-origin" always;

    # =========================================================================
    # Gzip 压缩配置
    # =========================================================================
    gzip on;
    gzip_min_length 1k;           # 最小压缩大小
    gzip_buffers 4 16k;           # 压缩缓冲区
    gzip_comp_level 6;            # 压缩级别 (1-9)
    gzip_types text/plain text/css text/javascript application/javascript application/json application/xml text/xml image/svg+xml;
    gzip_vary on;                 # 添加 Vary: Accept-Encoding 头
    gzip_proxied any;             # 对所有代理请求启用压缩

    # =========================================================================
    # 健康检查端点
    # =========================================================================
    location = /health {
        access_log off;
        return 200 "OK\n";
        add_header Content-Type text/plain;
    }

    # =========================================================================
    # WebSocket 代理
    # =========================================================================
    location /ws {
        proxy_pass http://backend_api;
        proxy_http_version 1.1;

        # WebSocket 升级头（必需）
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";

        # 透传客户端信息
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;

        # 长连接超时（1小时）
        proxy_connect_timeout 60s;
        proxy_send_timeout 3600s;
        proxy_read_timeout 3600s;

        # 禁用缓冲
        proxy_buffering off;
    }

    # =========================================================================
    # API 代理
    # =========================================================================
    location /api/v1/ {
        proxy_pass http://backend_api;
        proxy_http_version 1.1;

        # 请求头
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Request-ID $request_id;
        proxy_set_header Connection "";

        # 超时配置
        proxy_connect_timeout 30s;
        proxy_send_timeout 60s;
        proxy_read_timeout 60s;

        # 缓冲配置
        proxy_buffering on;
        proxy_buffer_size 4k;
        proxy_buffers 8 16k;
    }

    # =========================================================================
    # 静态资源缓存 - JS/CSS（1年强缓存）
    # =========================================================================
    location ~* \.(?:css|js)$ {
        expires 1y;
        add_header Cache-Control "public, max-age=31536000, immutable";
        access_log off;
    }

    # =========================================================================
    # 静态资源缓存 - 图片/字体（1年强缓存）
    # =========================================================================
    location ~* \.(?:jpg|jpeg|gif|png|ico|svg|webp|woff2?|ttf|eot)$ {
        expires 1y;
        add_header Cache-Control "public, max-age=31536000, immutable";
        access_log off;
    }

    # =========================================================================
    # PWA - Service Worker（不缓存）
    # =========================================================================
    location = /sw.js {
        add_header Cache-Control "no-cache, no-store, must-revalidate";
        expires 0;
        proxy_cache_bypass $http_pragma;
    }

    # =========================================================================
    # PWA - Manifest
    # =========================================================================
    location = /manifest.webmanifest {
        types {
            application/manifest+json manifest.webmanifest;
        }
        expires 1d;
    }

    # =========================================================================
    # Vue Router History 模式
    # 所有路由都返回 index.html，由前端路由处理
    # =========================================================================
    location / {
        try_files $uri $uri/ /index.html;
    }

    # =========================================================================
    # 禁止访问隐藏文件
    # =========================================================================
    location ~ /\. {
        deny all;
        access_log off;
        log_not_found off;
    }

    # 禁止访问敏感文件
    location ~ /\.(env|git|svn|htaccess) {
        deny all;
        access_log off;
        log_not_found off;
    }
}
```

### 7.2 关键配置说明

| 配置项 | 说明 |
|--------|------|
| `try_files $uri $uri/ /index.html` | Vue Router History 模式必需 |
| `Upgrade $http_upgrade` | WebSocket 升级必需 |
| `Connection "upgrade"` | WebSocket 连接保持 |
| `proxy_read_timeout 3600s` | WebSocket 长连接超时 |
| `expires 1y` + `immutable` | 静态资源强缓存 |
| `gzip_comp_level 6` | 压缩级别，平衡 CPU 和压缩率 |

---

## 8. 常见问题排查

### 8.1 刷新页面 404

**原因**：Vue Router 使用 History 模式，Nginx 未配置 `try_files`

**解决**：确保 Nginx 配置：

```nginx
location / {
    try_files $uri $uri/ /index.html;
}
```

### 8.2 API 请求 502 Bad Gateway

**排查步骤**：

```bash
# 1. 检查后端 Nginx 是否运行
curl http://127.0.0.1:10080/api/v1/services/health

# 2. 检查 Docker 容器状态
docker ps

# 3. 检查 Nginx 错误日志
tail -f /www/wwwlogs/game-platform-client.error.log
```

### 8.3 WebSocket 连接失败

**排查步骤**：

```bash
# 1. 检查 Nginx 版本（需要 1.3+）
nginx -v

# 2. 检查配置语法
nginx -t

# 3. 使用 wscat 测试
npm install -g wscat
wscat -c ws://43.143.124.220/ws
```

**常见错误**：

| 错误 | 原因 | 解决 |
|------|------|------|
| 400 Bad Request | 缺少 Upgrade 头 | 添加 `proxy_set_header Upgrade $http_upgrade;` |
| 502 Bad Gateway | 后端服务不可用 | 检查后端服务状态 |
| 连接超时 | 防火墙阻断 | 开放 80 端口 |

### 8.4 静态资源 404

**排查步骤**：

```bash
# 检查文件是否存在
ls -la /www/wwwroot/game-platform-client/

# 检查文件权限
stat /www/wwwroot/game-platform-client/index.html

# 修复权限
chown -R www:www /www/wwwroot/game-platform-client
chmod -R 755 /www/wwwroot/game-platform-client
```

### 8.5 PM2 服务无法启动

**排查步骤**：

```bash
# 查看详细错误
pm2 logs game-platform-client --err

# 手动测试启动
cd /www/wwwroot/game-platform-client
npm run preview

# 检查端口占用
netstat -tlnp | grep 3001
```

### 8.6 快速诊断命令

```bash
# 一键检查所有服务
echo "=== Nginx 状态 ===" && systemctl status nginx | head -3
echo "=== PM2 状态 ===" && pm2 status
echo "=== Docker 状态 ===" && docker ps --format "table {{.Names}}\t{{.Status}}"
echo "=== 端口监听 ===" && netstat -tlnp | grep -E ":(80|3001|10080)"
echo "=== 磁盘空间 ===" && df -h /www
echo "=== 最近错误 ===" && tail -20 /www/wwwlogs/game-platform-client.error.log
```

---

## 附录：快速部署命令参考

### 静态文件部署（一键脚本）

```bash
#!/bin/bash
# 文件: /www/wwwroot/deploy-static.sh

set -e

PROJECT_PATH="/www/wwwroot/game-platform-client"
BACKEND_NGINX="127.0.0.1:10080"
SERVER_NAME="43.143.124.220"

echo "========== 开始静态文件部署 =========="

# 1. 确保目录存在
mkdir -p $PROJECT_PATH
mkdir -p /www/wwwlogs/game-platform-client

# 2. 设置权限
chown -R www:www $PROJECT_PATH

# 3. 创建 Nginx 配置
cat > /www/server/panel/vhost/nginx/${SERVER_NAME}.conf << 'NGINX_EOF'
upstream backend_api {
    server 127.0.0.1:10080;
    keepalive 32;
}

server {
    listen 80;
    server_name 43.143.124.220;
    root /www/wwwroot/game-platform-client;
    index index.html;

    access_log /www/wwwlogs/game-platform-client.access.log;
    error_log /www/wwwlogs/game-platform-client.error.log;

    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;

    gzip on;
    gzip_min_length 1k;
    gzip_types text/plain text/css application/javascript application/json;

    location = /health {
        access_log off;
        return 200 "OK\n";
    }

    location /ws {
        proxy_pass http://backend_api;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 3600s;
        proxy_buffering off;
    }

    location /api/v1/ {
        proxy_pass http://backend_api;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header Connection "";
    }

    location ~* \.(css|js|jpg|jpeg|gif|png|ico|svg|woff2?)$ {
        expires 1y;
        add_header Cache-Control "public, immutable";
        access_log off;
    }

    location = /sw.js {
        add_header Cache-Control "no-cache";
        expires 0;
    }

    location / {
        try_files $uri $uri/ /index.html;
    }

    location ~ /\. {
        deny all;
    }
}
NGINX_EOF

# 4. 测试并重载 Nginx
nginx -t && nginx -s reload

echo "========== 部署完成 =========="
echo "请将 dist 目录内容上传到: $PROJECT_PATH"
echo "访问地址: http://$SERVER_NAME"
```

### PM2 部署（一键脚本）

```bash
#!/bin/bash
# 文件: /www/wwwroot/deploy-pm2.sh

set -e

PROJECT_PATH="/www/wwwroot/game-platform-client"

echo "========== 开始 PM2 部署 =========="

cd $PROJECT_PATH

# 1. 安装依赖
echo "[1/4] 安装依赖..."
npm install --registry=https://registry.npmmirror.com

# 2. 构建
echo "[2/4] 构建项目..."
npm run build

# 3. 重启 PM2
echo "[3/4] 重启服务..."
pm2 restart game-platform-client 2>/dev/null || pm2 start ecosystem.config.cjs

# 4. 保存 PM2
echo "[4/4] 保存 PM2 配置..."
pm2 save

echo "========== 部署完成 =========="
pm2 status
```
