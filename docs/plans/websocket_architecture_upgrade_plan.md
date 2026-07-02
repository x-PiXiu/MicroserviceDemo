# WebSocket 架构升级修改清单

## 文档信息

| 项目 | 内容 |
|------|------|
| 版本 | 2.5.3 |
| 创建日期 | 2026-02-21 |
| 最后更新 | 2026-02-22 |
| 关联文档 | `docs/client/client_operation_flow_guide.md` |
| 架构方案 | **WebSocket 优先模式 + HTTP API 并行** |
| 实现状态 | **P0/P1 已完成，P2 待实现** |

---

## 1. 架构变更概述

### 1.1 当前架构（已实现）

```
选择游戏 → 建立 WebSocket → 订阅房间列表 → WebSocket 创建/加入房间
                    ↓
              HTTP API（并行支持）
```

### 1.2 架构特点

| 特点 | 说明 |
|------|------|
| WebSocket 优先 | 客户端优先使用 WebSocket 进行所有游戏操作 |
| HTTP API 并行 | HTTP API 端点保留，与 WebSocket 并行运行 |
| 实时房间列表 | 客户端可实时看到房间创建、状态变化、销毁 |
| 更流畅体验 | 提前建立连接，进入房间无延迟 |
| 统一消息协议 | 减少客户端复杂度 |

---

## 2. 服务端修改清单

### 2.1 P0 - 核心功能 ✅ 已完成

#### 2.1.1 房间列表订阅与推送 ✅

**文件**: `src/game_services/gomoku/src/gomoku_websocket_handler.cpp`

**实现状态**: ✅ 已完成

**新增消息类型**:

| 消息类型 | 方向 | 说明 | 状态 |
|---------|------|------|------|
| `subscribe_room_list` | C→S | 订阅房间列表 | ✅ |
| `unsubscribe_room_list` | C→S | 取消订阅 | ✅ |
| `subscribe_confirm` | S→C | 订阅确认 | ✅ |
| `room_added` | S→C | 新房间创建通知 | ✅ |
| `room_updated` | S→C | 房间状态变化 | ✅ |
| `room_removed` | S→C | 房间销毁通知 | ✅ |

**实现位置**:
- `gomoku_websocket_handler.cpp:715-739` - handleSubscribeRoomList
- `gomoku_websocket_handler.cpp:744-754` - handleUnsubscribeRoomList
- `gomoku_server.cpp:912-940` - broadcastRoomEvent

---

#### 2.1.2 房间状态变化推送 ✅

**文件**: `src/game_services/gomoku/src/gomoku_room.cpp`

**实现状态**: ✅ 已完成

**触发场景**:
- ✅ 玩家加入房间 → `room_updated`
- ✅ 玩家离开房间 → `room_updated`
- ✅ 玩家准备状态变化 → `room_updated`
- ✅ 游戏开始 → `room_updated`
- ✅ 游戏结束 → `room_updated`
- ✅ 房间销毁 → `room_removed`

**实现位置**:
- `gomoku_server.cpp:511-518` - 玩家加入回调
- `gomoku_server.cpp:520-527` - 玩家离开回调
- `gomoku_server.cpp:563-567` - 玩家准备回调
- `gomoku_server.cpp:570-574` - 游戏开始回调
- `gomoku_server.cpp:540-559` - 游戏结束回调
- `gomoku_server.cpp:501-508` - 房间销毁回调

---

#### 2.1.3 HTTP API 路径参数 ✅

**文件**: `src/game_services/gomoku/src/gomoku_server.cpp`

**实现状态**: ✅ 已完成

**修复内容**: 路径模式从 `/api/gomoku/rooms/:room_id` 修正为 `/api/v1/gomoku/rooms/:room_id`

---

### 2.2 P1 - 重要功能 ✅ 已完成

#### 2.2.1 心跳机制增强 ✅

**文件**: `src/game_services/game_base/src/websocket_handler_base.cpp`

**实现状态**: ✅ 已完成

**实现内容**:
```cpp
struct HeartbeatConfig {
    int interval_ms = 30000;      // 心跳间隔
    int timeout_ms = 60000;       // 超时时间
    int max_missed = 3;           // 最大丢失数
};
```

**实现位置**:
- `websocket_handler_base.h:89-105` - HeartbeatConfig 结构
- `player_session_base.h` - missed_heartbeats_ 计数器

---

#### 2.2.2 断线重连状态恢复 ✅

**文件**: `src/game_services/gomoku/src/gomoku_server.cpp`

**实现状态**: ✅ 已完成

**实现内容**:
```cpp
struct DisconnectedPlayerInfo {
    std::string room_id;
    std::chrono::system_clock::time_point disconnect_time;
    nlohmann::json player_state;
};
// 重连超时: 5分钟 (RECONNECT_TIMEOUT_SECONDS = 300)
```

**实现位置**:
- `gomoku_server.h:444-451` - DisconnectedPlayerInfo 结构
- `gomoku_websocket_handler.cpp:759-788` - handleReconnect

---

#### 2.2.3 玩家在线状态管理 ✅

**文件**: `src/game_services/gomoku/src/gomoku_server.cpp`

**实现状态**: ✅ 已完成

**实现内容**:
```cpp
struct OnlinePlayerInfo {
    std::string player_id;
    std::chrono::system_clock::time_point online_time;
    std::string current_room_id;
    nlohmann::json player_data;
};
```

**实现位置**:
- `gomoku_server.h:458-465` - OnlinePlayerInfo 结构
- `gomoku_server.h:323-365` - 在线状态管理方法

---

### 2.3 P2 - 优化功能 ❌ 待实现

#### 2.3.1 WebSocket 连接状态跟踪 ❌

**状态**: ❌ 未实现

**计划消息类型**:

| 消息类型 | 方向 | 说明 |
|---------|------|------|
| `connection_status` | S→C | 连接状态变化 |
| `latency_check` | C→S | 延迟检测请求 |
| `latency_result` | S→C | 延迟检测结果 |

---

#### 2.3.2 消息队列与离线消息 ❌

**状态**: ❌ 未实现

**计划功能**:
- 为每个玩家维护最近 N 条消息
- 重连后发送离线消息
- 消息确认机制

---

## 3. 数据结构变更 ✅ 已实现

### 3.1 订阅者管理 ✅

```cpp
// gomoku_server.h (已实现)
class GomokuServer {
private:
    std::set<std::string> room_list_subscribers_;
    mutable std::mutex subscribers_mutex_;

public:
    void subscribeRoomList(const std::string& player_id);
    void unsubscribeRoomList(const std::string& player_id);
    void broadcastToSubscribers(const nlohmann::json& message);
};
```

### 3.2 在线玩家管理 ✅

```cpp
// gomoku_server.h (已实现)
struct OnlinePlayerInfo {
    std::string player_id;
    std::chrono::system_clock::time_point online_time;
    std::string current_room_id;
    nlohmann::json player_data;
};

std::unordered_map<std::string, OnlinePlayerInfo> online_players_;
mutable std::shared_mutex online_players_mutex_;
```

### 3.3 断线玩家管理 ✅

```cpp
// gomoku_server.h (已实现)
struct DisconnectedPlayerInfo {
    std::string room_id;
    std::chrono::system_clock::time_point disconnect_time;
    nlohmann::json player_state;
};

std::unordered_map<std::string, DisconnectedPlayerInfo> disconnected_players_;
static constexpr int RECONNECT_TIMEOUT_SECONDS = 300;  // 5分钟
```

---

## 4. 消息协议定义

### 4.1 房间列表订阅 ✅ 已实现

```json
// 客户端发送
{
  "type": "subscribe_room_list",
  "data": {
    "filters": {
      "gameMode": "freestyle"
    }
  }
}

// 服务器响应 ✅
{
  "type": "subscribe_confirm",
  "data": {
    "rooms": [...],
    "subscriptionId": "sub_1234567890_usr_xxx"
  }
}
```

### 4.2 房间列表更新 ✅ 已实现

```json
// 新房间创建 ✅
{
  "type": "room_added",
  "data": {
    "room": {
      "room_id": "gomoku_xxx",
      "creator_id": "usr_xxx",
      "current_players": 1,
      "state": "waiting_players"
    }
  }
}

// 房间状态变化 ✅
{
  "type": "room_updated",
  "data": {
    "roomId": "gomoku_xxx",
    "changes": {
      "current_players": 2,
      "state": "ready"
    }
  }
}

// 房间销毁 ✅
{
  "type": "room_removed",
  "data": {
    "roomId": "gomoku_xxx",
    "reason": "room_closed"
  }
}
```

---

## 5. 实施状态总结

### 5.1 实施进度

| 阶段 | 状态 | 完成度 |
|------|------|--------|
| 阶段一：核心功能 | ✅ 完成 | 100% |
| 阶段二：稳定性增强 | ✅ 完成 | 100% |
| 阶段三：体验优化 | ❌ 未开始 | 0% |

### 5.2 功能清单

| 功能 | 优先级 | 状态 | 文件 |
|------|--------|------|------|
| HTTP API 路径参数修复 | P0 | ✅ | `gomoku_server.cpp` |
| 房间列表订阅 | P0 | ✅ | `gomoku_websocket_handler.cpp` |
| 房间状态变化广播 | P0 | ✅ | `gomoku_room.cpp` |
| 心跳机制增强 | P1 | ✅ | `websocket_handler_base.cpp` |
| 断线重连支持 | P1 | ✅ | `gomoku_server.cpp` |
| 玩家在线状态 | P1 | ✅ | `gomoku_server.cpp` |
| 时间控制与更新 | P1 | ✅ | `gomoku_logic.cpp`, `gomoku_room.cpp` |
| 和棋请求-响应 | P1 | ✅ | `gomoku_room.cpp` |
| 悔棋请求-响应 | P1 | ✅ | `gomoku_room.cpp` |
| 断线判负 | P1 | ✅ | `gomoku_room.cpp` |
| 认输功能 | P1 | ✅ | `gomoku_room.cpp` |
| 连接状态跟踪 | P2 | ❌ | - |
| 消息队列缓存 | P2 | ❌ | - |

---

## 6. 多玩家房间交互流程

### 6.1 完整流程时序图

```
玩家A (创建者)                 服务器                      玩家B (加入者)
    │                           │                              │
    │  1. WebSocket 连接        │                              │
    │ ─────────────────────────►│                              │
    │                           │                              │
    │  2. welcome               │                              │
    │ ◄─────────────────────────│                              │
    │                           │                              │
    │  3. authenticate          │                              │
    │ ─────────────────────────►│                              │
    │                           │                              │
    │  4. auth_success          │                              │
    │ ◄─────────────────────────│                              │
    │                           │                              │
    │  5. subscribe_room_list   │                              │
    │ ─────────────────────────►│                              │
    │                           │                              │
    │  6. subscribe_confirm     │                              │
    │ ◄─────────────────────────│                              │
    │                           │                              │
    │  7. create_room           │                              │
    │ ─────────────────────────►│                              │
    │                           │  创建 GomokuRoom 实例         │
    │                           │                              │
    │  8. room_created          │                              │
    │ ◄─────────────────────────│                              │
    │                           │                              │
    │                           │  9. room_added (广播)        │
    │                           │ ────────────────────────────►│
    │                           │                              │
    │                           │  10. join_room               │
    │                           │ ◄────────────────────────────│
    │                           │                              │
    │  11. player_joined        │  12. room_joined             │
    │ ◄─────────────────────────│─────────────────────────────►│
    │                           │                              │
    │                           │  13. room_updated (广播)      │
    │                           │ ────────────────────────────►│
    │                           │                              │
    │  ... 游戏流程 ...          │                              │
```

---

## 7. 房间操作消息格式

### 7.1 创建房间 (create_room / room_created) ✅

**客户端发送**:
```json
{
  "type": "create_room",
  "data": {
    "creatorId": "usr_xxx",
    "config": {
      "gameMode": "freestyle",
      "timeLimit": 900,
      "incrementPerMove": 10,
      "allowUndo": false,
      "allowSpectators": true,
      "maxSpectators": 50
    }
  }
}
```

**服务器响应（给创建者）**:
```json
{
  "type": "room_created",
  "data": {
    "roomId": "gomoku_xxx",
    "creatorId": "usr_xxx",
    "pieceType": "black",
    "config": {
      "gameMode": 0,
      "gameModeStr": "freestyle",
      "timeLimit": 900,
      "incrementPerMove": 10,
      "allowUndo": false,
      "allowSpectators": true,
      "maxSpectators": 50
    }
  },
  "timestamp": 1234567890123
}
```

### 7.2 开始游戏 (start_game) ✅ 手动开始

**重要变更**: 游戏不再自动开始，需要房主手动点击"开始游戏"按钮。

**客户端发送（仅房主可调用）**:
```json
{
  "type": "room_operation",
  "data": {
    "operation": "start_game",
    "roomId": "gomoku_xxx"
  }
}
```

**服务器响应（成功）**:
```json
{
  "type": "operation_result",
  "data": {
    "operation": "start_game",
    "success": true,
    "roomId": "gomoku_xxx"
  }
}
```

**服务器响应（失败）**:
```json
{
  "type": "operation_result",
  "data": {
    "operation": "start_game",
    "success": false,
    "error": "NOT_CREATOR",
    "message": "Only room creator can start the game"
  }
}
```

**开始条件**:
1. 调用者必须是房间创建者
2. 房间内必须有 2 名玩家
3. 所有玩家都必须已准备
4. 房间状态必须是 `WAITING`

---

## 8. 游戏内容推送消息格式

### 8.1 玩家加入 (player_joined) ✅

```json
{
  "type": "player_joined",
  "data": {
    "playerId": "usr_xxx",
    "roomId": "gomoku_xxx",
    "pieceType": 2,
    "playerCount": 2,
    "playerInfo": {
      "playerId": "usr_xxx",
      "username": "player_name",
      "nickname": "玩家昵称",
      "avatarUrl": "https://...",
      "level": 10,
      "rating": 1500,
      "totalGames": 100,
      "wins": 60,
      "losses": 40
    }
  }
}
```

### 8.2 玩家离开 (player_left) ✅

```json
{
  "type": "player_left",
  "data": {
    "playerId": "usr_xxx",
    "roomId": "gomoku_xxx",
    "playerCount": 1
  }
}
```

### 8.3 游戏结束 (game_end) ✅

```json
{
  "type": "game_end",
  "data": {
    "result": 1,
    "resultStr": "BLACK_WIN",
    "winnerId": "usr_xxx",
    "winnerPiece": 1,
    "reason": "五子连珠",
    "stats": {
      "totalMoves": 45,
      "duration": 320,
      "blackTimeLeft": 500,
      "whiteTimeLeft": 300
    }
  }
}
```

### 8.4 时间更新 (time_update) ✅

```json
{
  "type": "time_update",
  "data": {
    "blackTime": 899,
    "whiteTime": 900
  },
  "roomId": "gomoku_xxx",
  "timestamp": 1234567890123
}
```

---

## 9. 游戏核心功能调用链

### 9.1 时间控制调用链 ✅

**功能说明**: 游戏开始后，每秒广播 `time_update` 消息，更新双方剩余时间。

**配置**:
| 配置项 | 值 | 说明 |
|-------|-----|------|
| `time_update_interval_ms_` | `1000` | 时间更新间隔（毫秒） |

**完整调用链**:

```
游戏开始
    │
    ▼
GomokuRoom::startGameSpecific()
    │
    ├─> gomoku_logic_->startGame()
    │
    └─> gomoku_logic_->startTimeControl()
            │
            ▼
        GomokuLogic::startTimer()
            │
            ├─> last_move_time_ = now  (设置基准时间)
            │
            └─> event_loop->runEvery(1000ms, timerCallback)
                                            │
                    ┌───────────────────────┘
                    │
                    ▼ (每1秒触发)
            GomokuLogic::timerCallback()
                    │
                    ├─> if (!time_counting_.load()) return
                    │
                    └─> if (isGameRunning())
                            │
                            ▼
                    GomokuLogic::updateTime()
                            │
                            ├─> elapsed = now - last_move_time_ (约1秒)
                            │
                            ├─> timeLeft = timeLeft - elapsed.count() (减少1秒)
                            │
                            ├─> if (timeLeft <= 0) handleTimeout()
                            │
                            ├─> last_move_time_ = now (重置基准)
                            │
                            └─> notifyTimeUpdate(piece, timeLeft)
                                    │
                                    ▼
                            time_update_callback_(piece, timeLeft)
                                    │
                                    ▼ (由 GomokuRoom 设置)
                            GomokuRoom::onTimeUpdate(piece, timeLeft)
                                    │
                                    ▼
                            GomokuRoom::broadcastTimeUpdate()
                                    │
                                    ├─> time_data = {
                                    │       "blackTime": getTimeLeft(BLACK),
                                    │       "whiteTime": getTimeLeft(WHITE)
                                    │   }
                                    │
                                    └─> broadcastToPlayers(time_update message)
```

**实现位置**:
- `gomoku_logic.h:218` - `time_update_interval_ms_ = 1000`
- `gomoku_logic.cpp:493-521` - `startTimer()`
- `gomoku_logic.cpp:542-556` - `timerCallback()`
- `gomoku_logic.cpp:472-491` - `updateTime()`
- `gomoku_room.cpp:1128-1132` - 回调设置
- `gomoku_room.cpp:1357-1359` - `onTimeUpdate()`
- `gomoku_room.cpp:1231-1241` - `broadcastTimeUpdate()`

**游戏结束时停止**:
```
GomokuRoom::endGameSpecific()
    │
    └─> gomoku_logic_->stopTimeControl()
            │
            ▼
        GomokuLogic::stopTimer()
            │
            ├─> time_counting_.store(false)
            │
            └─> event_loop->cancelTimer(time_control_timer_id_)
```

---

### 9.2 和棋请求-响应调用链 ✅

**功能说明**: 玩家发起和棋请求，对手响应后服务器广播结果。

**调用链**:

```
玩家A 发起和棋
    │
    ▼
客户端发送: { type: "draw_offer", data: {} }
    │
    ▼
GomokuRoom::processGameActionSpecific()
    │
    └─> handleDrawOffer(player_id)
            │
            ├─> gomoku_logic_->offerDraw(player_id)
            │       │
            │       └─> 设置 draw_offered_ = true
            │
            └─> sendToPlayer(opponent_id, {
                    type: "draw_offer",
                    data: { fromPlayer: player_id }
                })
                    │
                    ▼
            玩家B 收到和棋请求
                    │
                    ▼
            客户端发送: { type: "draw_response", data: { accepted: true } }
                    │
                    ▼
            GomokuRoom::processGameActionSpecific()
                    │
                    └─> handleDrawResponse(player_id, accept)
                            │
                            ├─> gomoku_logic_->respondToDraw(player_id, accept)
                            │
                            ├─> broadcastToPlayers({
                            │       type: "draw_response",
                            │       data: { fromPlayer, accepted }
                            │   })
                            │
                            └─> if (accepted)
                                    handleGameEnd(GameResult::DRAW)
```

**实现位置**:
- `gomoku_room.cpp:187-201` - 消息解析（支持 `accepted` 字段）
- `gomoku_room.cpp:735-752` - `handleDrawOffer()`
- `gomoku_room.cpp:754-774` - `handleDrawResponse()`

---

### 9.3 悔棋请求-响应调用链 ✅

**功能说明**: 玩家发起悔棋请求，对手响应后服务器执行或拒绝。

**调用链**:

```
玩家A 发起悔棋
    │
    ▼
客户端发送: { type: "undo_move", data: {} }
    │
    ▼
GomokuRoom::processGameActionSpecific()
    │
    └─> handleUndoRequest(player_id)
            │
            ├─> 检查 gomoku_config_.allowUndo
            │
            ├─> 检查是否有待处理的悔棋请求
            │
            ├─> 设置 pending_undo_requester_ = player_id
            │       has_pending_undo_request_ = true
            │
            └─> sendToPlayer(opponent_id, {
                    type: "undo_request",
                    data: { fromPlayer: player_id }
                })
                    │
                    ▼
            玩家B 收到悔棋请求
                    │
                    ▼
            客户端发送: { type: "undo_response", data: { accepted: true } }
                    │
                    ▼
            GomokuRoom::processGameActionSpecific()
                    │
                    └─> handleUndoResponse(player_id, accept)
                            │
                            ├─> 验证响应者是否为对手
                            │
                            ├─> if (accepted)
                            │       ├─> gomoku_logic_->undoMove()
                            │       └─> broadcastToPlayers({
                            │               type: "move_undone",
                            │               data: { playerId, board, totalMoves }
                            │           })
                            │
                            └─> 清理 pending_undo_requester_
```

**实现位置**:
- `gomoku_room.cpp:168-180` - 消息解析
- `gomoku_room.cpp:640-678` - `handleUndoRequest()`
- `gomoku_room.cpp:680-713` - `handleUndoResponse()`

---

### 9.4 断线判负调用链 ✅

**功能说明**: 游戏进行中玩家断开连接，对手自动获胜。

**调用链**:

```
玩家断开连接
    │
    ▼
WebSocketHandlerBase::handleWebSocketDisconnection()
    │
    ├─> removePlayerSession(player_id)
    │
    └─> connection_callback_(player_id, false)
            │
            ▼ (由 GomokuServer 设置)
    GomokuServer::onPlayerConnectionChange(player_id, false)
            │
            ├─> 获取玩家所在房间
            │
            └─> room->removePlayer(player_id)
                    │
                    ▼
            GameRoomBase::removePlayer()
                    │
                    ├─> players_.erase(player_id)
                    │
                    └─> onPlayerLeft(player_id)
                            │
                            ▼
                    GomokuRoom::onPlayerLeft(player_id)
                            │
                            ├─> 清理玩家棋子分配
                            │
                            └─> if (getState() == PLAYING)
                                    │
                                    ├─> opponent_id = getOpponentId(player_id)
                                    │
                                    └─> endGame({opponent_id})  // 对手获胜
                                            │
                                            ▼
                                    handleGameEnd(GameResult::SURRENDER)
                                            │
                                            ▼
                                    broadcastToPlayers({
                                        type: "game_end",
                                        data: {
                                            result: winner_piece,
                                            resultStr: "BLACK_WIN/WHITE_WIN",
                                            winnerId: opponent_id,
                                            reason: "对手断线"
                                        }
                                    })
```

**实现位置**:
- `websocket_handler_base.cpp:1130-1192` - `handleWebSocketDisconnection()`
- `gomoku_server.cpp:530-560` - 连接变化回调
- `game_room_base.cpp:86-130` - `removePlayer()`
- `gomoku_room.cpp:1037-1080` - `onPlayerLeft()`

---

### 9.5 认输调用链 ✅

**功能说明**: 玩家主动认输，对手获胜。

**调用链**:

```
玩家发送认输
    │
    ▼
客户端发送: { type: "surrender", data: {} }
    │
    ▼
GomokuRoom::processGameActionSpecific()
    │
    └─> handleSurrender(player_id)
            │
            ├─> piece = getPlayerPiece(player_id)
            │
            ├─> broadcastToPlayers({
            │       type: "player_surrendered",
            │       data: { playerId, piece }
            │   })
            │
            └─> handleGameEnd(GameResult::SURRENDER)
                    │
                    ▼
            broadcastGameEnd()
                    │
                    └─> broadcastToPlayers({
                            type: "game_end",
                            data: { result, winnerId, reason: "对手认输" }
                        })
```

**实现位置**:
- `gomoku_room.cpp:181-183` - 消息解析
- `gomoku_room.cpp:715-733` - `handleSurrender()`

---

### 9.6 落子调用链 ✅

**功能说明**: 玩家在棋盘上落子，服务器验证并广播结果。

**调用链**:

```
玩家发送落子
    │
    ▼
客户端发送: { type: "place_piece", data: { position: { row, col } } }
    │
    ▼
GomokuRoom::processGameActionSpecific()
    │
    └─> handlePlacePiece(player_id, position)
            │
            ├─> 验证玩家在房间内
            │
            └─> gomoku_logic_->placePiece(player_id, position)
                    │
                    ├─> 验证玩家ID有效
                    │
                    ├─> 验证是当前玩家回合
                    │
                    ├─> 验证位置有效且为空
                    │
                    ├─> 检查禁手（如果有禁手规则）
                    │
                    ├─> board.setPiece(position, piece)
                    │
                    ├─> 添加到移动历史
                    │
                    ├─> 检查获胜
                    │       │
                    │       └─> if (checkWin())
                    │               endGame(result)
                    │
                    ├─> 切换当前玩家
                    │
                    └─> notifyGameStateUpdate()
                            │
                            ▼
                    state_update_callback_(state)
                            │
                            ▼ (由 GomokuRoom 设置)
                    GomokuRoom::onGameStateUpdate(state)
                            │
                            ▼
                    GomokuRoom::broadcastMoveResult()
                            │
                            └─> broadcastToPlayers({
                                    type: "move_result",
                                    data: { playerId, position, piece, success }
                                })
```

**实现位置**:
- `gomoku_room.cpp:151-166` - 消息解析
- `gomoku_room.cpp:580-638` - `handlePlacePiece()`
- `gomoku_logic.cpp:157-263` - `placePiece()`
- `gomoku_room.cpp:1130-1144` - `broadcastMoveResult()`

---

## 10. 测试清单

### 10.1 功能测试

- [ ] 房间列表订阅/取消订阅
- [ ] 房间创建时广播通知
- [ ] 玩家加入/离开时状态更新
- [ ] 游戏开始/结束时状态更新
- [ ] 断线重连后状态恢复
- [ ] 时间更新每秒广播
- [ ] 和棋请求-响应流程
- [ ] 悔棋请求-响应流程
- [ ] 断线判负流程
- [ ] 认输流程
- [ ] 落子流程

### 10.2 压力测试

- [ ] 1000+ 并发 WebSocket 连接
- [ ] 房间列表高频更新
- [ ] 心跳风暴测试

### 10.3 兼容性测试

- [ ] HTTP API 并行可用
- [ ] Nginx WebSocket 代理正常
- [ ] 跨域请求处理

---

## 11. 已移除的功能

以下旧架构相关功能已被清理：

| 功能 | 说明 |
|------|------|
| API Gateway 注册 | 已移除，使用 Nginx + Service Registry |
| 回滚配置 | 已移除 `websocket_mode` 配置结构 |

---

**文档版本**: 2.5.3
**最后更新**: 2026-02-22
**更新内容**:
- **v2.5.3**: 修复加入房间消息格式
  - 将服务端的 `room_info` 改为 `room_joined`
  - 添加顶层 `pieceType` 字段
- **v2.5.2**: 添加游戏核心功能调用链文档
  - 添加 §9 游戏核心功能调用链（时间控制、和棋、悔棋、断线判负、认输、落子）
  - 添加 `time_update` 消息格式
  - 更新测试清单
- **v2.5.1**: 修复 `draw_response` 字段名解析问题
  - 服务器正确解析 `accepted` 字段
- **v2.5.0**: 修复 `time_update` 消息未发送问题
  - 添加 `startTimeControl()` / `stopTimeControl()` 方法
  - 移除 `tickGameSpecific()` 中的 `updateTime()` 调用
- **v2.4.0**: 修复服务端消息格式与文档一致
  - `move_result` 添加 `piece` 字段
  - `player_surrendered` 添加 `piece` 字段
  - `move_undone` 添加 `board`, `totalMoves` 字段
  - `game_started` / `game_state` 添加完整字段
- **v2.3.0**: 添加手动开始游戏功能（start_game 操作），移除自动开始逻辑
- **v2.2.0**: 完成 P0/P1 功能实现
- 添加房间创建消息格式 (create_room / room_created)
- 添加 playerInfo 字段到 player_joined 消息
- 更新消息格式符合文档规范
- 移除回滚配置，简化架构
- 清理旧架构代码（API Gateway 相关）
