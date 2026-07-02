# 数据库 ER 图（Mermaid 格式）

> 使用 Mermaid Live Editor (https://mermaid.live) 渲染，或直接在支持 Mermaid 的 Markdown 编辑器中预览。

---

## 图4-3 系统总ER图（数据库级关系）

```mermaid
%%{init: {"theme": "default", "themeVariables": { "fontSize": "18px", "fontFamily": "arial" }}}%%
erDiagram
    USER_SERVICE_DB {
        string database_name "user_service_db"
        string responsibility "用户核心数据"
        int table_count "5"
    }
    AUTH_SESSIONS_DB {
        string database_name "auth_sessions_db"
        string responsibility "认证与会话"
        int table_count "6"
    }
    GAME_SERVICE_DB {
        string database_name "game_service_db"
        string responsibility "游戏通用数据"
        int table_count "10"
    }
    GOMOKU_GAME_DB {
        string database_name "gomoku_game_db"
        string responsibility "五子棋业务数据"
        int table_count "7"
    }

    USER_SERVICE_DB ||--o{ AUTH_SESSIONS_DB : "user_id 逻辑关联"
    USER_SERVICE_DB ||--o{ GAME_SERVICE_DB : "user_id 逻辑关联"
    USER_SERVICE_DB ||--o{ GOMOKU_GAME_DB : "user_id 逻辑关联"
    GAME_SERVICE_DB ||--o{ GOMOKU_GAME_DB : "game_type_id 关联"
```

---

## 图4-4 认证服务数据库ER图（auth_sessions_db）

```mermaid
%%{init: {"theme": "default", "themeVariables": { "fontSize": "18px", "fontFamily": "arial" }}}%%
erDiagram
    USER_SESSIONS {
        string session_id PK
        string user_id FK
        string device_type
        string client_ip
        string expires_at
        boolean is_active
    }
    JWT_BLACKLIST {
        string token_id PK
        string user_id FK
        string token_type "access or refresh"
        string expires_at
    }
    AUTH_AUDIT_LOGS {
        int log_id PK
        string user_id FK
        string action "login register etc"
        boolean success
        string error_code
    }
    LOGIN_RATE_LIMITS {
        int limit_id PK
        string identifier
        string identifier_type "ip or username or device"
        int attempt_count
        boolean is_blocked
    }
    SECURITY_EVENTS {
        int event_id PK
        string user_id FK
        string event_type
        string severity "low medium high critical"
        float risk_score "0.00 to 1.00"
    }
    AUTH_CONFIG {
        int config_id PK
        string config_key UK
        string config_value
        string config_type "string number boolean json"
    }

    USER_SESSIONS ||--o{ JWT_BLACKLIST : "user_id"
    USER_SESSIONS ||--o{ AUTH_AUDIT_LOGS : "user_id"
    USER_SESSIONS ||--o{ SECURITY_EVENTS : "user_id"
```

---

## 图4-5 用户服务数据库ER图（user_service_db）

```mermaid
%%{init: {"theme": "default", "themeVariables": { "fontSize": "18px", "fontFamily": "arial" }}}%%
erDiagram
    USERS {
        string user_id PK
        string username UK
        string email UK
        string password_hash
        int status "0未激活 1活跃 2暂停 3封禁"
        int online_status "0离线 1在线"
        int login_attempts
    }
    USER_PROFILES {
        string user_id PK
        string nickname
        string avatar_url
        string bio
        string language
    }
    USER_PREFERENCES {
        string user_id PK
        string theme "light or dark"
        boolean sound_effects
        string custom_settings
    }
    USER_SESSIONS {
        string session_id PK
        string user_id FK
        string device_type
        string client_ip
        boolean is_active
    }
    USER_AUDIT_LOGS {
        int log_id PK
        string user_id FK
        string action
        string result
        string details
    }

    USERS ||--|| USER_PROFILES : "1对1"
    USERS ||--|| USER_PREFERENCES : "1对1"
    USERS ||--o{ USER_SESSIONS : "1对N"
    USERS ||--o{ USER_AUDIT_LOGS : "1对N"
```

---

## 图4-6 游戏数据服务数据库ER图（game_service_db）

```mermaid
%%{init: {"theme": "default", "themeVariables": { "fontSize": "18px", "fontFamily": "arial" }}}%%
erDiagram
    USER_GAME_PROFILES {
        string user_id PK
        int game_type_id PK
        int current_rating
        int peak_rating
        int total_games
        int wins
    }
    ACHIEVEMENT_DEFINITIONS {
        string achievement_id PK
        string name
        string achievement_type "progress milestone hidden"
        string rarity "common rare epic legendary"
        int points
    }
    USER_ACHIEVEMENTS {
        string user_id PK
        string achievement_id PK
        int current_progress
        boolean is_unlocked
    }
    CURRENCY_DEFINITIONS {
        int currency_type_id PK
        string currency_code UK
        string name
        boolean is_premium
    }
    USER_CURRENCY {
        string user_id PK
        int currency_type_id PK
        int amount
        int total_earned
        int total_spent
    }
    CURRENCY_TRANSACTIONS {
        string transaction_id PK
        string user_id FK
        int currency_type_id FK
        string transaction_type "earn spend transfer"
        int balance_before
        int balance_after
    }

    USER_GAME_PROFILES ||--o{ USER_ACHIEVEMENTS : ""
    USER_GAME_PROFILES ||--o{ USER_CURRENCY : ""
    ACHIEVEMENT_DEFINITIONS ||--o{ USER_ACHIEVEMENTS : ""
    CURRENCY_DEFINITIONS ||--o{ USER_CURRENCY : ""
    CURRENCY_DEFINITIONS ||--o{ CURRENCY_TRANSACTIONS : ""
```

---

## 图4-7 五子棋游戏服务数据库ER图（gomoku_game_db）

**左侧：用户与房间**

```mermaid
%%{init: {"theme": "default", "themeVariables": { "fontSize": "18px", "fontFamily": "arial" }}}%%
erDiagram
    GOMOKU_USERS {
        string user_id PK
        string username
        int current_rating
        int total_games
        int wins
    }
    GOMOKU_ROOMS {
        string room_id PK
        string creator_id FK
        string game_mode "freestyle renju swap2"
        string room_state "waiting playing finished"
        int version "乐观锁"
    }

    GOMOKU_USERS ||--o{ GOMOKU_ROOMS : ""
```

**右侧：对局与落子**

```mermaid
%%{init: {"theme": "default", "themeVariables": { "fontSize": "18px", "fontFamily": "arial" }}}%%
erDiagram
    GOMOKU_GAMES {
        int game_id PK
        string room_id FK
        string black_player_id FK
        string white_player_id FK
        string game_result "black_win white_win draw"
        string winner_id
        string win_type "five_in_row timeout"
        int total_moves
    }
    GOMOKU_MOVES {
        int move_id PK
        int game_id FK
        int move_number
        string player_id
        int position "row*15+col"
        int time_used_ms
    }

    GOMOKU_GAMES ||--o{ GOMOKU_MOVES : ""
```

**关系说明：**

| 关系 | 类型 | 说明 |
|------|------|------|
| GOMOKU_USERS → GOMOKU_ROOMS | 1:N | 用户创建房间 |
| GOMOKU_ROOMS → GOMOKU_GAMES | 1:N | 房间内进行多局对弈 |
| GOMOKU_GAMES → GOMOKU_MOVES | 1:N | 一局对弈包含多步落子 |
| GOMOKU_USERS → GOMOKU_GAMES | 1:N | 用户作为玩家参与对局 |
