-- =============================================================================
-- 五子棋游戏服务数据库设计 (重构版)
-- 版本: 2.0.0 
-- 修复日期: 2025-09-27
-- 描述: 解决跨数据库外键、数据冗余、一致性问题
-- 修复原则: 
--   1. 移除跨数据库外键引用
--   2. 减少数据冗余，专注游戏数据
--   3. 使用事件驱动架构保证数据一致性
--   4. 简化表结构，提高性能
--   5. 统一线程池管理，避免资源浪费
-- =============================================================================

-- 创建数据库
CREATE DATABASE IF NOT EXISTS `gomoku_game_db` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE `gomoku_game_db`;

-- =============================================================================
-- 🔧 修复1: 用户游戏统计表 - 必需的本地缓存表
-- =============================================================================

-- 用户游戏统计表（本地缓存表）
-- 注意：这是从用户服务同步过来的缓存数据，减少跨服务调用
CREATE TABLE `gomoku_users` (
    `user_id` VARCHAR(64) PRIMARY KEY NOT NULL COMMENT '用户ID（从用户服务同步）',
    `username` VARCHAR(50) NOT NULL COMMENT '用户名',
    `nickname` VARCHAR(100) DEFAULT NULL COMMENT '昵称',
    
    -- 游戏相关统计（本服务维护）
    `current_rating` INT DEFAULT 1500 COMMENT '当前评分',
    `peak_rating` INT DEFAULT 1500 COMMENT '历史最高评分',
    `total_games` INT DEFAULT 0 COMMENT '总游戏数',
    `wins` INT DEFAULT 0 COMMENT '胜局数',
    `losses` INT DEFAULT 0 COMMENT '败局数',
    `draws` INT DEFAULT 0 COMMENT '平局数',
    `total_playtime_minutes` INT DEFAULT 0 COMMENT '总游戏时长（分钟）',
    
    -- 状态信息
    `last_game_at` TIMESTAMP NULL COMMENT '最后游戏时间',
    `is_active` BOOLEAN DEFAULT TRUE COMMENT '是否活跃',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    
    -- 索引优化
    INDEX idx_rating (`current_rating`),
    INDEX idx_games (`total_games`),
    INDEX idx_active (`is_active`),
    INDEX idx_last_game (`last_game_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户游戏统计表（本地缓存）';

-- =============================================================================
-- 🔧 修复2: 游戏房间表 - 移除用户信息冗余
-- =============================================================================

-- 游戏房间表（简化版）
CREATE TABLE `gomoku_rooms` (
    `room_id` VARCHAR(64) PRIMARY KEY NOT NULL COMMENT '房间ID',
    `room_name` VARCHAR(100) DEFAULT NULL COMMENT '房间名称',
    `creator_id` VARCHAR(64) NOT NULL COMMENT '创建者ID（只存储ID，不建外键）',
    `game_mode` ENUM('freestyle', 'renju', 'swap2', 'pro', 'tournament') DEFAULT 'freestyle' COMMENT '游戏模式',
    `room_type` ENUM('casual', 'ranked', 'tournament', 'private') DEFAULT 'casual' COMMENT '房间类型',
    `is_private` BOOLEAN DEFAULT FALSE COMMENT '是否私人房间',
    `password_hash` VARCHAR(255) DEFAULT NULL COMMENT '房间密码哈希',
    `max_spectators` INT DEFAULT 50 COMMENT '最大观战人数',
    `allow_spectators` BOOLEAN DEFAULT TRUE COMMENT '是否允许观战',
    
    -- 游戏配置（JSON格式，避免配置表的复杂性）
    `game_config` JSON NOT NULL COMMENT '游戏配置（JSON格式）',
    
    -- 状态记录
    `room_state` ENUM('waiting', 'playing', 'paused', 'finished', 'abandoned') DEFAULT 'waiting' COMMENT '房间状态',
    `player_count` TINYINT UNSIGNED DEFAULT 0 COMMENT '当前玩家数量',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `started_at` TIMESTAMP NULL COMMENT '开始游戏时间',
    `finished_at` TIMESTAMP NULL COMMENT '结束时间',
    `last_activity_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '最后活动时间',
    
    -- 🔧 修复：添加版本号支持乐观锁，避免并发问题
    `version` INT UNSIGNED DEFAULT 1 COMMENT '版本号（乐观锁）',
    
    -- 索引优化
    INDEX idx_creator (`creator_id`),
    INDEX idx_game_mode (`game_mode`),
    INDEX idx_room_state (`room_state`),
    INDEX idx_created_at (`created_at`),
    INDEX idx_last_activity (`last_activity_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='五子棋房间表（简化版）';

-- =============================================================================
-- 🔧 修复2: 游戏记录表 - 专注游戏数据，移除积分管理
-- =============================================================================

-- 游戏记录表（简化版）
CREATE TABLE `gomoku_games` (
    `game_id` BIGINT PRIMARY KEY AUTO_INCREMENT COMMENT '游戏ID',
    `room_id` VARCHAR(64) NOT NULL COMMENT '房间ID',
    `black_player_id` VARCHAR(64) NOT NULL COMMENT '黑方玩家ID（只存ID）',
    `white_player_id` VARCHAR(64) NOT NULL COMMENT '白方玩家ID（只存ID）',
    `game_mode` ENUM('freestyle', 'renju', 'swap2', 'pro', 'tournament') NOT NULL COMMENT '游戏模式',
    `game_type` ENUM('casual', 'ranked', 'tournament', 'friendly') NOT NULL COMMENT '游戏类型',
    
    -- 游戏结果（简化）
    `game_result` ENUM('black_win', 'white_win', 'draw', 'black_timeout', 'white_timeout', 
                      'black_surrender', 'white_surrender', 'abandoned') NULL COMMENT '游戏结果',
    `winner_id` VARCHAR(64) NULL COMMENT '获胜者ID',
    `win_type` ENUM('five_in_row', 'timeout', 'surrender', 'draw', 'abandoned') NULL COMMENT '获胜方式',
    `winning_line` JSON NULL COMMENT '获胜线路坐标(JSON格式)',
    
    -- 游戏统计
    `total_moves` INT UNSIGNED DEFAULT 0 COMMENT '总步数',
    `game_duration_seconds` INT UNSIGNED DEFAULT 0 COMMENT '游戏持续时间(秒)',
    `black_time_used_seconds` INT UNSIGNED DEFAULT 0 COMMENT '黑方用时(秒)',
    `white_time_used_seconds` INT UNSIGNED DEFAULT 0 COMMENT '白方用时(秒)',
    
    -- 🔧 修复：移除积分相关字段，这些应该由游戏数据服务管理
    -- 移除: black_rating_before, black_rating_after, white_rating_before, white_rating_after, rating_change
    
    -- 游戏配置快照（简化）
    `final_board_state` JSON NULL COMMENT '最终棋盘状态',
    
    -- 时间记录
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `started_at` TIMESTAMP NULL COMMENT '开始时间',
    `finished_at` TIMESTAMP NULL COMMENT '结束时间',
    
    -- 服务器信息
    `server_id` VARCHAR(64) DEFAULT NULL COMMENT '服务器ID',
    `server_version` VARCHAR(20) DEFAULT '2.0.0' COMMENT '服务器版本',
    
    -- 🔧 修复：移除跨数据库外键约束
    -- FOREIGN KEY (`room_id`) REFERENCES `gomoku_rooms`(`room_id`) ON DELETE CASCADE,
    -- 使用应用层保证数据一致性
    
    -- 索引优化
    INDEX idx_room_id (`room_id`),
    INDEX idx_black_player (`black_player_id`),
    INDEX idx_white_player (`white_player_id`),
    INDEX idx_winner (`winner_id`),
    INDEX idx_game_result (`game_result`),
    INDEX idx_finished_at (`finished_at`),
    INDEX idx_game_mode_type (`game_mode`, `game_type`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='五子棋游戏记录表（简化版）';

-- =============================================================================
-- 🔧 修复3: 移动记录表 - 优化存储，减少冗余
-- =============================================================================

-- 移动记录表（优化版）
CREATE TABLE `gomoku_moves` (
    `move_id` BIGINT PRIMARY KEY AUTO_INCREMENT COMMENT '移动ID',
    `game_id` BIGINT NOT NULL COMMENT '游戏ID',
    `move_number` SMALLINT UNSIGNED NOT NULL COMMENT '步数序号(1-225)',
    `player_id` VARCHAR(64) NOT NULL COMMENT '玩家ID',
    `piece_type` ENUM('black', 'white') NOT NULL COMMENT '棋子类型',
    `position` SMALLINT UNSIGNED NOT NULL COMMENT '位置编码（row*15+col，0-224）',
    
    -- 时间信息（简化）
    `move_timestamp` TIMESTAMP(3) DEFAULT CURRENT_TIMESTAMP(3) COMMENT '落子时间戳（毫秒精度）',
    `time_used_ms` INT UNSIGNED DEFAULT 0 COMMENT '本步思考时间(毫秒)',
    `remaining_time_seconds` INT DEFAULT 0 COMMENT '剩余时间(秒)',
    
    -- 🔧 修复：移除复杂的游戏状态存储，减少存储空间
    -- 移除: board_state JSON（占用空间大，查询慢）
    -- 移除: position_evaluation, is_winning_move, is_mistake（AI分析数据应该独立存储）
    
    -- 禁手信息（仅连珠模式）
    `is_forbidden` BOOLEAN DEFAULT FALSE COMMENT '是否禁手',
    `forbidden_type` ENUM('three_three', 'four_four', 'overline') NULL COMMENT '禁手类型',
    
    -- 外键约束（本地数据库内）
    FOREIGN KEY (`game_id`) REFERENCES `gomoku_games`(`game_id`) ON DELETE CASCADE,
    
    -- 唯一约束和索引优化
    UNIQUE KEY uk_game_move (`game_id`, `move_number`),
    INDEX idx_game_id (`game_id`),
    INDEX idx_player_id (`player_id`),
    INDEX idx_move_timestamp (`move_timestamp`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='五子棋移动记录表（优化版）';

-- =============================================================================
-- 🔧 修复4: 简化观战和聊天表
-- =============================================================================

-- 观战记录表（简化版）
CREATE TABLE `gomoku_spectators` (
    `id` BIGINT PRIMARY KEY AUTO_INCREMENT COMMENT 'ID',
    `game_id` BIGINT NOT NULL COMMENT '游戏ID',
    `user_id` VARCHAR(64) NOT NULL COMMENT '观战用户ID',
    `join_time` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '加入观战时间',
    `leave_time` TIMESTAMP NULL COMMENT '离开观战时间',
    `watch_duration_seconds` INT UNSIGNED DEFAULT 0 COMMENT '观战时长(秒)',
    
    FOREIGN KEY (`game_id`) REFERENCES `gomoku_games`(`game_id`) ON DELETE CASCADE,
    INDEX idx_game_user (`game_id`, `user_id`),
    INDEX idx_join_time (`join_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='观战记录表（简化版）';

-- 聊天记录表（简化版）
CREATE TABLE `gomoku_chat_messages` (
    `message_id` BIGINT PRIMARY KEY AUTO_INCREMENT COMMENT '消息ID',
    `room_id` VARCHAR(64) NOT NULL COMMENT '房间ID',
    `user_id` VARCHAR(64) NOT NULL COMMENT '发送者ID',
    `message_type` ENUM('room', 'system') DEFAULT 'room' COMMENT '消息类型（简化）',
    `content` VARCHAR(500) NOT NULL COMMENT '消息内容（限制长度）',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '发送时间',
    
    -- 🔧 移除复杂的私信功能，专注房间聊天
    INDEX idx_room_time (`room_id`, `created_at`),
    INDEX idx_user_id (`user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='聊天消息表（简化版）';

-- =============================================================================
-- 🔧 修复5: 移除跨服务同步表，使用消息队列
-- =============================================================================

-- 🔧 删除 gomoku_sync_log 表
-- 跨服务数据同步改为使用消息队列（如RabbitMQ、Kafka）
-- 优势：
-- 1. 解耦服务间依赖
-- 2. 提供重试和死信队列机制
-- 3. 支持事件驱动架构
-- 4. 更好的性能和可扩展性

-- =============================================================================
-- 🔧 修复6: 简化配置表
-- =============================================================================

-- 游戏配置表（简化版）
CREATE TABLE `gomoku_configs` (
    `config_id` BIGINT PRIMARY KEY AUTO_INCREMENT COMMENT '配置ID',
    `config_name` VARCHAR(50) NOT NULL UNIQUE COMMENT '配置名称',
    `config_data` JSON NOT NULL COMMENT '配置数据',
    `is_active` BOOLEAN DEFAULT TRUE COMMENT '是否启用',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    
    INDEX idx_name_active (`config_name`, `is_active`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='游戏配置表（简化版）';

-- =============================================================================
-- 🔧 修复7: 服务器状态表 - 监控优化
-- =============================================================================

-- 服务器状态记录表（优化版）
CREATE TABLE `gomoku_server_status` (
    `id` BIGINT PRIMARY KEY AUTO_INCREMENT COMMENT 'ID',
    `server_id` VARCHAR(64) NOT NULL COMMENT '服务器ID',
    `status` ENUM('starting', 'running', 'stopping', 'stopped', 'error') NOT NULL COMMENT '服务器状态',
    `active_rooms` INT UNSIGNED DEFAULT 0 COMMENT '活跃房间数',
    `active_games` INT UNSIGNED DEFAULT 0 COMMENT '活跃游戏数',
    `connected_players` INT UNSIGNED DEFAULT 0 COMMENT '连接玩家数',
    `memory_usage_mb` INT UNSIGNED DEFAULT 0 COMMENT '内存使用量(MB)',
    `uptime_seconds` INT UNSIGNED DEFAULT 0 COMMENT '运行时间(秒)',
    -- 🔧 新增：线程池监控
    `thread_pool_size` INT UNSIGNED DEFAULT 0 COMMENT '线程池大小',
    `active_threads` INT UNSIGNED DEFAULT 0 COMMENT '活跃线程数',
    `queue_size` INT UNSIGNED DEFAULT 0 COMMENT '任务队列大小',
    `recorded_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '记录时间',
    
    -- 只保留最近24小时的数据
    INDEX idx_server_time (`server_id`, `recorded_at`),
    INDEX idx_recorded_at (`recorded_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='服务器状态记录（优化版）';

-- =============================================================================
-- 🔧 修复8: 插入默认配置数据
-- =============================================================================

-- 插入默认游戏配置
INSERT INTO `gomoku_configs` (`config_name`, `config_data`) VALUES
('freestyle_default', JSON_OBJECT(
    'gameMode', 'freestyle', 
    'timeLimit', 900, 
    'incrementPerMove', 10, 
    'allowUndo', false,
    'allowSpectators', true,
    'maxSpectators', 50
)),
('renju_default', JSON_OBJECT(
    'gameMode', 'renju', 
    'timeLimit', 1200, 
    'incrementPerMove', 15, 
    'allowUndo', false,
    'allowSpectators', true,
    'maxSpectators', 50,
    'forbiddenRules', true
)),
('blitz_mode', JSON_OBJECT(
    'gameMode', 'freestyle', 
    'timeLimit', 300, 
    'incrementPerMove', 3, 
    'allowUndo', false,
    'allowSpectators', true,
    'maxSpectators', 100
)),
('tournament_mode', JSON_OBJECT(
    'gameMode', 'tournament', 
    'timeLimit', 1800, 
    'incrementPerMove', 20, 
    'allowUndo', false,
    'allowSpectators', true,
    'maxSpectators', 200
));

-- 插入默认线程池配置
INSERT INTO `gomoku_configs` (`config_name`, `config_data`) VALUES
('thread_pool_config', JSON_OBJECT(
    'pool_size', 8,
    'max_queue_size', 1000,
    'enable_monitoring', true,
    'task_timeout_seconds', 30
));

-- =============================================================================
-- 🔧 修复9: 创建视图简化查询
-- =============================================================================

-- 活跃房间视图
CREATE VIEW `active_rooms` AS
SELECT 
    `room_id`,
    `room_name`,
    `creator_id`,
    `game_mode`,
    `room_type`,
    `room_state`,
    `player_count`,
    `created_at`
FROM `gomoku_rooms`
WHERE `room_state` IN ('waiting', 'playing')
ORDER BY `created_at` DESC;

-- 最近完成的游戏视图
CREATE VIEW `recent_games` AS
SELECT 
    `game_id`,
    `room_id`,
    `black_player_id`,
    `white_player_id`,
    `game_result`,
    `winner_id`,
    `total_moves`,
    `game_duration_seconds`,
    `finished_at`
FROM `gomoku_games`
WHERE `finished_at` IS NOT NULL
ORDER BY `finished_at` DESC
LIMIT 100;

-- 线程池状态视图
CREATE VIEW `thread_pool_status` AS
SELECT 
    `server_id`,
    `thread_pool_size`,
    `active_threads`,
    `queue_size`,
    ROUND(`active_threads` / `thread_pool_size` * 100, 2) AS `thread_utilization_percent`,
    `recorded_at`
FROM `gomoku_server_status`
WHERE `recorded_at` >= DATE_SUB(NOW(), INTERVAL 1 HOUR)
ORDER BY `recorded_at` DESC;

-- =============================================================================
-- 🔧 修复10: 定期清理任务
-- =============================================================================

-- 创建清理过期数据的存储过程
DELIMITER $$

CREATE PROCEDURE `CleanupOldData`()
BEGIN
    DECLARE cleaned_count INT DEFAULT 0;
    
    -- 清理7天前的聊天记录
    DELETE FROM `gomoku_chat_messages` 
    WHERE `created_at` < DATE_SUB(NOW(), INTERVAL 7 DAY);
    SET cleaned_count = cleaned_count + ROW_COUNT();
    
    -- 清理30天前的观战记录
    DELETE FROM `gomoku_spectators` 
    WHERE `join_time` < DATE_SUB(NOW(), INTERVAL 30 DAY);
    SET cleaned_count = cleaned_count + ROW_COUNT();
    
    -- 清理24小时前的服务器状态记录
    DELETE FROM `gomoku_server_status` 
    WHERE `recorded_at` < DATE_SUB(NOW(), INTERVAL 24 HOUR);
    SET cleaned_count = cleaned_count + ROW_COUNT();
    
    -- 清理已结束超过30天的房间
    DELETE FROM `gomoku_rooms` 
    WHERE `room_state` = 'finished' 
    AND `finished_at` < DATE_SUB(NOW(), INTERVAL 30 DAY);
    SET cleaned_count = cleaned_count + ROW_COUNT();
    
    SELECT cleaned_count as total_cleaned_records;
END$$

-- 线程池监控更新存储过程
CREATE PROCEDURE `UpdateThreadPoolStatus`(
    IN p_server_id VARCHAR(64),
    IN p_pool_size INT,
    IN p_active_threads INT,
    IN p_queue_size INT
)
BEGIN
    INSERT INTO `gomoku_server_status` (
        `server_id`, 
        `status`, 
        `thread_pool_size`, 
        `active_threads`, 
        `queue_size`
    ) VALUES (
        p_server_id,
        'running',
        p_pool_size,
        p_active_threads,
        p_queue_size
    );
END$$

DELIMITER ;

-- 创建定期清理事件
CREATE EVENT IF NOT EXISTS `evt_cleanup_old_data`
ON SCHEDULE EVERY 1 DAY
STARTS '2025-09-27 03:00:00'
DO
  CALL CleanupOldData();

-- =============================================================================
-- 🔧 修复11: 性能优化索引
-- =============================================================================

-- 复合索引优化查询性能
CREATE INDEX idx_games_player_result ON gomoku_games (black_player_id, game_result, finished_at);
CREATE INDEX idx_games_player_result_white ON gomoku_games (white_player_id, game_result, finished_at);
CREATE INDEX idx_games_mode_time ON gomoku_games (game_mode, finished_at DESC);

-- 移动记录查询优化
CREATE INDEX idx_moves_game_player ON gomoku_moves (game_id, player_id, move_number);

-- 房间查询优化
CREATE INDEX idx_rooms_type_state ON gomoku_rooms (room_type, room_state, created_at);

-- =============================================================================
-- 🔧 修复12: 触发器确保数据一致性
-- =============================================================================

DELIMITER $$

-- 游戏结束时自动更新房间状态
CREATE TRIGGER `trg_game_finish_update_room` 
    AFTER UPDATE ON `gomoku_games`
    FOR EACH ROW
BEGIN
    IF NEW.game_result IS NOT NULL AND OLD.game_result IS NULL THEN
        UPDATE `gomoku_rooms` 
        SET `room_state` = 'finished', 
            `finished_at` = NOW()
        WHERE `room_id` = NEW.room_id;
    END IF;
END$$

-- 移动记录插入时更新游戏统计
CREATE TRIGGER `trg_move_update_game_stats`
    AFTER INSERT ON `gomoku_moves`
    FOR EACH ROW
BEGIN
    UPDATE `gomoku_games` 
    SET `total_moves` = `total_moves` + 1
    WHERE `game_id` = NEW.game_id;
END$$

DELIMITER ;

-- =============================================================================
-- 完成信息
-- =============================================================================

SELECT 
    'Gomoku Database Fixed Successfully' as status,
    NOW() as completion_time,
    '修复内容: 移除跨数据库外键, 简化表结构, 优化索引, 统一线程池管理' as description,
    '主要表: rooms, games, moves, spectators, chat, configs, server_status' as main_tables,
    '新功能: 线程池监控, 事件驱动架构, 性能优化' as new_features;