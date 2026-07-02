# 游戏平台客户端

基于 Vue 3 + TypeScript + Vite 的在线游戏平台客户端应用。

## 技术栈

- **Vue 3.4+** - 渐进式 JavaScript 框架
- **TypeScript 5.0+** - 类型安全的 JavaScript 超集
- **Vite 5.0+** - 下一代前端构建工具
- **Pinia 2.1+** - Vue 3 官方状态管理库
- **Vue Router 4.2+** - Vue 3 官方路由管理
- **Axios 1.6+** - HTTP 客户端
- **Socket.io-client 4.7+** - WebSocket 客户端
- **Element Plus 2.4+** - Vue 3 UI 组件库
- **TailwindCSS 3.4+** - 原子化 CSS 框架
- **VueUse 10.0+** - Vue 组合式工具集

## 项目结构

```
src/
├── api/                    # API 层
├── assets/                 # 静态资源
├── components/             # 组件
│   ├── common/             # 通用组件
│   ├── gomoku/             # 五子棋组件
│   └── mobile/             # 移动端组件
├── composables/            # 组合式函数
├── layouts/                # 布局组件
├── locales/                # 国际化
├── router/                 # 路由
├── stores/                 # 状态管理
├── types/                  # TypeScript 类型
├── utils/                  # 工具函数
├── views/                  # 页面视图
├── websocket/              # WebSocket 模块
├── App.vue
└── main.ts
```

## 开始使用

### 安装依赖

```bash
npm install
```

### 开发模式

```bash
npm run dev
```

应用将在 http://localhost:3000 启动。

### 构建生产版本

```bash
npm run build
```

### 预览生产构建

```bash
npm run preview
```

### 代码检查

```bash
npm run lint
```

### 代码格式化

```bash
npm run format
```

### 类型检查

```bash
npm run typecheck
```

### E2E 测试

```bash
npm run test:e2e
```

## 功能特性

### 用户认证
- 用户登录/注册
- Token 自动刷新
- 记住登录状态

### 游戏大厅
- 房间列表浏览
- 创建游戏房间
- 加入游戏房间
- 快速匹配

### 五子棋游戏
- 15x15 标准棋盘
- 实时对战
- 回合计时
- 棋谱记录
- 聊天功能

### 排行榜
- 积分排行
- 胜场排行
- 胜率排行

### 用户中心
- 个人资料管理
- 头像上传
- 游戏统计
- 偏好设置

### 其他
- 深色/浅色主题切换
- 多语言支持（中文/英文）
- PWA 支持
- 响应式设计
