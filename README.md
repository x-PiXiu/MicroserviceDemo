# MicroserviceDemo

C++ 微服务游戏平台 —— 本科毕业设计作品，也是《[领导请批示 · 开发周报](https://space.bilibili.com/)》系列视频中"前任留下的网络库"的血统来源。

## 项目简介

一套以自研 epoll 网络库为底座的微服务游戏平台，包含完整的账号、认证、数据与实时对局链路：

| 服务 | 职责 |
|------|------|
| **user_service** | 用户注册 / 登录 / 资料管理（MySQL 持久化 + Redis 缓存） |
| **auth_service** | JWT 签发与校验、分布式会话管理 |
| **game_data_service** | 对局数据 / 战绩存储与查询 |
| **gomoku_server** | 五子棋实时对局服务（长连接 + 房间管理） |

## 技术栈

- **网络**：自研 epoll 网络库（Channel / EventLoop / 层级时间轮定时器 / 事件驱动）
- **存储**：MySQL 连接池、Redis 连接池、Repository 模式封装
- **中间件**：Kafka 事件总线、服务注册中心客户端
- **安全**：JWT、IP 黑名单、MD5/Crypto 工具集
- **部署**：Docker 多阶段构建 + docker-compose，MySQL 初始化脚本
- **前端**：game-platform-client（Vue / React）

## 目录结构

```
├── include/common/         # 公共库：network / logger / http / websocket / database / repository ...
├── src/common/             # 公共库实现
├── src/core_services/      # 微服务入口（user / auth / game_data / gomoku）
├── config/                 # 各服务配置
├── docker/                 # 多阶段构建与编排
├── docs/                   # API 文档 / 架构设计 / 毕业设计文档
└── game-platform-client/   # Web 前端
```

## 构建

```bash
# Docker 一键部署
cd docker/multi-stage && docker-compose up -d

# 或本地 cmake
cmake -B build && cmake --build build -j
```
