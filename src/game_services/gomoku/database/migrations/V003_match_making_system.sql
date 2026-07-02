-- =============================================================================
-- 五子棋游戏服务 - 自动匹配系统数据库迁移
-- 版本: V003
-- 创建日期: 2025-01-23
-- 描述: 为自动匹配系统添加必要的数据库表和索引
-- =============================================================================

USE `gomoku_game_db`;

-- =============================================================================
-- 1. 匹配记录表
-- =============================================================================

-- 匹配记录表：记录每次匹配的详细信息
CREATE TABLE IF NOT EXISTS `gomoku_match_records` (
    `id` BIGINT PRIMARY KEY AUTO_INCREMENT COMMENT '记录ID',
    `match_id` VARCHAR(64) NOT NULL COMMENT '匹配ID',
    `match_mode` ENUM('casual', 'ranked') NOT NULL COMMENT '匹配模式',
    `game_mode` ENUM('freestyle', 'renju', 'swap2', 'pro', 'tournament') NOT NULL DEFAULT 'freestyle' COMMENT '游戏规则',

    -- 玩家信息
    `player1_id` VARCHAR(64) NOT NULL COMMENT '玩家1用户ID',
    `player1_rating` INT NOT NULL DEFAULT 1200 COMMENT '玩家1匹配时评分',
    `player1_wait_seconds` INT NOT NULL DEFAULT 0 COMMENT '玩家1等待时间(秒)',
    `player2_id` VARCHAR(64) NOT NULL COMMENT '玩家2用户ID',
    `player2_rating` INT NOT NULL DEFAULT 1200 COMMENT '玩家2匹配时评分',
    `player2_wait_seconds` INT NOT NULL DEFAULT 0 COMMENT '玩家2等待时间(秒)',

    -- 匹配质量
    `rating_difference` INT NOT NULL DEFAULT 0 COMMENT '评分差异',
    `match_quality` ENUM('excellent', 'good', 'fair', 'acceptable') NOT NULL DEFAULT 'acceptable' COMMENT '匹配质量',

    -- 关联信息
    `room_id` VARCHAR(64) NULL COMMENT '分配的房间ID',
    `game_id` BIGINT NULL COMMENT '关联的游戏ID',

    -- 时间信息
    `matched_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '匹配完成时间',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',

    -- 索引
    UNIQUE KEY uk_match_id (`match_id`),
    INDEX idx_player1 (`player1_id`),
    INDEX idx_player2 (`player2_id`),
    INDEX idx_match_mode (`match_mode`),
    INDEX idx_match_quality (`match_quality`),
    INDEX idx_matched_at (`matched_at`),
    INDEX idx_room_id (`room_id`),
    INDEX idx_players_time (`player1_id`, `player2_id`, `matched_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='匹配记录表';

-- =============================================================================
-- 2. 匹配池快照表
-- =============================================================================

-- 匹配池快照表：定期记录匹配池状态，用于统计和分析
CREATE TABLE IF NOT EXISTS `gomoku_match_pool_snapshots` (
    `id` BIGINT PRIMARY KEY AUTO_INCREMENT COMMENT '快照ID',
    `snapshot_time` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '快照时间',

    -- 池状态
    `total_requests` INT NOT NULL DEFAULT 0 COMMENT '总请求数',
    `waiting_requests` INT NOT NULL DEFAULT 0 COMMENT '等待中请求数',
    `ranked_count` INT NOT NULL DEFAULT 0 COMMENT '竞技模式数量',
    `casual_count` INT NOT NULL DEFAULT 0 COMMENT '休闲模式数量',

    -- 评分分布
    `rating_min` INT NOT NULL DEFAULT 0 COMMENT '最低评分',
    `rating_max` INT NOT NULL DEFAULT 0 COMMENT '最高评分',
    `rating_avg` DECIMAL(10,2) NOT NULL DEFAULT 0 COMMENT '平均评分',

    -- 等待时间统计
    `avg_wait_seconds` INT NOT NULL DEFAULT 0 COMMENT '平均等待时间',
    `max_wait_seconds` INT NOT NULL DEFAULT 0 COMMENT '最大等待时间',

    -- 索引
    INDEX idx_snapshot_time (`snapshot_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='匹配池快照表';

-- =============================================================================
-- 3. 匹配统计表
-- =============================================================================

-- 每日匹配统计表
CREATE TABLE IF NOT EXISTS `gomoku_match_daily_stats` (
    `id` BIGINT PRIMARY KEY AUTO_INCREMENT COMMENT '统计ID',
    `stat_date` DATE NOT NULL COMMENT '统计日期',

    -- 匹配数量
    `total_matches` INT NOT NULL DEFAULT 0 COMMENT '总匹配数',
    `ranked_matches` INT NOT NULL DEFAULT 0 COMMENT '竞技模式匹配数',
    `casual_matches` INT NOT NULL DEFAULT 0 COMMENT '休闲模式匹配数',

    -- 质量分布
    `excellent_matches` INT NOT NULL DEFAULT 0 COMMENT '优秀匹配数',
    `good_matches` INT NOT NULL DEFAULT 0 COMMENT '良好匹配数',
    `fair_matches` INT NOT NULL DEFAULT 0 COMMENT '一般匹配数',
    `acceptable_matches` INT NOT NULL DEFAULT 0 COMMENT '可接受匹配数',

    -- 等待时间统计
    `avg_wait_seconds` INT NOT NULL DEFAULT 0 COMMENT '平均等待时间',
    `max_wait_seconds` INT NOT NULL DEFAULT 0 COMMENT '最大等待时间',
    `total_wait_seconds` BIGINT NOT NULL DEFAULT 0 COMMENT '总等待时间',

    -- 请求统计
    `total_requests` INT NOT NULL DEFAULT 0 COMMENT '总请求数',
    `cancelled_requests` INT NOT NULL DEFAULT 0 COMMENT '取消请求数',
    `timeout_requests` INT NOT NULL DEFAULT 0 COMMENT '超时请求数',

    -- 评分统计
    `avg_rating_diff` DECIMAL(10,2) NOT NULL DEFAULT 0 COMMENT '平均评分差',

    -- 时间戳
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',

    -- 唯一索引：每天一条记录
    UNIQUE KEY uk_stat_date (`stat_date`),
    INDEX idx_created_at (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='每日匹配统计表';

-- =============================================================================
-- 4. 玩家匹配历史表
-- =============================================================================

-- 玩家匹配历史表：记录每个玩家的匹配历史，用于分析和推荐
CREATE TABLE IF NOT EXISTS `gomoku_player_match_history` (
    `id` BIGINT PRIMARY KEY AUTO_INCREMENT COMMENT '历史ID',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID',
    `match_id` VARCHAR(64) NOT NULL COMMENT '匹配ID',

    -- 匹配信息
    `match_mode` ENUM('casual', 'ranked') NOT NULL COMMENT '匹配模式',
    `opponent_id` VARCHAR(64) NOT NULL COMMENT '对手ID',
    `rating_before` INT NOT NULL DEFAULT 1200 COMMENT '匹配前评分',
    `opponent_rating` INT NOT NULL DEFAULT 1200 COMMENT '对手评分',
    `rating_difference` INT NOT NULL DEFAULT 0 COMMENT '评分差异',
    `match_quality` ENUM('excellent', 'good', 'fair', 'acceptable') NOT NULL COMMENT '匹配质量',

    -- 等待时间
    `wait_seconds` INT NOT NULL DEFAULT 0 COMMENT '等待时间(秒)',

    -- 结果（游戏结束后更新）
    `game_result` ENUM('win', 'loss', 'draw', 'abandoned') NULL COMMENT '游戏结果',
    `game_id` BIGINT NULL COMMENT '关联游戏ID',

    -- 时间
    `matched_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '匹配时间',
    `game_finished_at` TIMESTAMP NULL COMMENT '游戏结束时间',

    -- 索引
    INDEX idx_user_id (`user_id`),
    INDEX idx_opponent_id (`opponent_id`),
    INDEX idx_match_id (`match_id`),
    INDEX idx_matched_at (`matched_at`),
    INDEX idx_user_time (`user_id`, `matched_at`),
    INDEX idx_user_opponent (`user_id`, `opponent_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='玩家匹配历史表';

-- =============================================================================
-- 5. 创建视图
-- =============================================================================

-- 最近匹配记录视图
CREATE OR REPLACE VIEW `v_recent_matches` AS
SELECT
    mr.match_id,
    mr.match_mode,
    mr.game_mode,
    mr.player1_id,
    mr.player2_id,
    mr.player1_rating,
    mr.player2_rating,
    mr.rating_difference,
    mr.match_quality,
    mr.room_id,
    mr.matched_at,
    u1.username as player1_name,
    u2.username as player2_name
FROM `gomoku_match_records` mr
LEFT JOIN `gomoku_users` u1 ON mr.player1_id = u1.user_id
LEFT JOIN `gomoku_users` u2 ON mr.player2_id = u2.user_id
ORDER BY mr.matched_at DESC
LIMIT 100;

-- 玩家匹配统计视图
CREATE OR REPLACE VIEW `v_player_match_stats` AS
SELECT
    user_id,
    COUNT(*) as total_matches,
    SUM(CASE WHEN match_quality = 'excellent' THEN 1 ELSE 0 END) as excellent_matches,
    SUM(CASE WHEN match_quality = 'good' THEN 1 ELSE 0 END) as good_matches,
    AVG(wait_seconds) as avg_wait_seconds,
    AVG(rating_difference) as avg_rating_diff,
    MAX(matched_at) as last_match_time
FROM `gomoku_player_match_history`
GROUP BY user_id;

-- =============================================================================
-- 6. 创建存储过程
-- =============================================================================

DELIMITER $$

-- 更新每日统计存储过程
CREATE PROCEDURE `SP_UpdateDailyMatchStats`(IN p_date DATE)
BEGIN
    INSERT INTO `gomoku_match_daily_stats` (
        stat_date,
        total_matches,
        ranked_matches,
        casual_matches,
        excellent_matches,
        good_matches,
        fair_matches,
        acceptable_matches,
        avg_wait_seconds,
        max_wait_seconds,
        total_wait_seconds,
        total_requests,
        avg_rating_diff
    )
    SELECT
        p_date,
        COUNT(*),
        SUM(CASE WHEN match_mode = 'ranked' THEN 1 ELSE 0 END),
        SUM(CASE WHEN match_mode = 'casual' THEN 1 ELSE 0 END),
        SUM(CASE WHEN match_quality = 'excellent' THEN 1 ELSE 0 END),
        SUM(CASE WHEN match_quality = 'good' THEN 1 ELSE 0 END),
        SUM(CASE WHEN match_quality = 'fair' THEN 1 ELSE 0 END),
        SUM(CASE WHEN match_quality = 'acceptable' THEN 1 ELSE 0 END),
        IFNULL(AVG((player1_wait_seconds + player2_wait_seconds) / 2), 0),
        IFNULL(MAX(GREATEST(player1_wait_seconds, player2_wait_seconds)), 0),
        IFNULL(SUM(player1_wait_seconds + player2_wait_seconds), 0),
        COUNT(DISTINCT player1_id) + COUNT(DISTINCT player2_id),
        IFNULL(AVG(rating_difference), 0)
    FROM `gomoku_match_records`
    WHERE DATE(matched_at) = p_date
    ON DUPLICATE KEY UPDATE
        total_matches = VALUES(total_matches),
        ranked_matches = VALUES(ranked_matches),
        casual_matches = VALUES(casual_matches),
        excellent_matches = VALUES(excellent_matches),
        good_matches = VALUES(good_matches),
        fair_matches = VALUES(fair_matches),
        acceptable_matches = VALUES(acceptable_matches),
        avg_wait_seconds = VALUES(avg_wait_seconds),
        max_wait_seconds = VALUES(max_wait_seconds),
        total_wait_seconds = VALUES(total_wait_seconds),
        total_requests = VALUES(total_requests),
        avg_rating_diff = VALUES(avg_rating_diff),
        updated_at = NOW();
END$$

-- 记录匹配存储过程
CREATE PROCEDURE `SP_RecordMatch`(
    IN p_match_id VARCHAR(64),
    IN p_match_mode VARCHAR(20),
    IN p_game_mode VARCHAR(20),
    IN p_player1_id VARCHAR(64),
    IN p_player1_rating INT,
    IN p_player1_wait_seconds INT,
    IN p_player2_id VARCHAR(64),
    IN p_player2_rating INT,
    IN p_player2_wait_seconds INT,
    IN p_match_quality VARCHAR(20),
    IN p_room_id VARCHAR(64)
)
BEGIN
    DECLARE v_rating_diff INT;

    SET v_rating_diff = ABS(p_player1_rating - p_player2_rating);

    -- 插入匹配记录
    INSERT INTO `gomoku_match_records` (
        match_id, match_mode, game_mode,
        player1_id, player1_rating, player1_wait_seconds,
        player2_id, player2_rating, player2_wait_seconds,
        rating_difference, match_quality, room_id
    ) VALUES (
        p_match_id, p_match_mode, p_game_mode,
        p_player1_id, p_player1_rating, p_player1_wait_seconds,
        p_player2_id, p_player2_rating, p_player2_wait_seconds,
        v_rating_diff, p_match_quality, p_room_id
    );

    -- 插入玩家1历史
    INSERT INTO `gomoku_player_match_history` (
        user_id, match_id, match_mode, opponent_id,
        rating_before, opponent_rating, rating_difference,
        match_quality, wait_seconds
    ) VALUES (
        p_player1_id, p_match_id, p_match_mode, p_player2_id,
        p_player1_rating, p_player2_rating, v_rating_diff,
        p_match_quality, p_player1_wait_seconds
    );

    -- 插入玩家2历史
    INSERT INTO `gomoku_player_match_history` (
        user_id, match_id, match_mode, opponent_id,
        rating_before, opponent_rating, rating_difference,
        match_quality, wait_seconds
    ) VALUES (
        p_player2_id, p_match_id, p_match_mode, p_player1_id,
        p_player2_rating, p_player1_rating, v_rating_diff,
        p_match_quality, p_player2_wait_seconds
    );
END$$

-- 记录匹配池快照存储过程
CREATE PROCEDURE `SP_RecordPoolSnapshot`(
    IN p_total_requests INT,
    IN p_waiting_requests INT,
    IN p_ranked_count INT,
    IN p_casual_count INT,
    IN p_rating_min INT,
    IN p_rating_max INT,
    IN p_rating_avg DECIMAL(10,2),
    IN p_avg_wait_seconds INT,
    IN p_max_wait_seconds INT
)
BEGIN
    INSERT INTO `gomoku_match_pool_snapshots` (
        total_requests, waiting_requests,
        ranked_count, casual_count,
        rating_min, rating_max, rating_avg,
        avg_wait_seconds, max_wait_seconds
    ) VALUES (
        p_total_requests, p_waiting_requests,
        p_ranked_count, p_casual_count,
        p_rating_min, p_rating_max, p_rating_avg,
        p_avg_wait_seconds, p_max_wait_seconds
    );
END$$

DELIMITER ;

-- =============================================================================
-- 7. 创建定时事件
-- =============================================================================

-- 每日统计更新事件（每天凌晨1点执行）
CREATE EVENT IF NOT EXISTS `evt_update_daily_match_stats`
ON SCHEDULE EVERY 1 DAY
STARTS CONCAT(CURDATE() + INTERVAL 1 DAY, ' 01:00:00')
DO
    CALL SP_UpdateDailyMatchStats(CURDATE() - INTERVAL 1 DAY);

-- 匹配池快照事件（每5分钟执行一次）
CREATE EVENT IF NOT EXISTS `evt_record_pool_snapshot`
ON SCHEDULE EVERY 5 MINUTE
STARTS NOW()
DO
    INSERT INTO `gomoku_match_pool_snapshots` (total_requests, waiting_requests, ranked_count, casual_count,
        rating_min, rating_max, rating_avg, avg_wait_seconds, max_wait_seconds)
    SELECT 0, 0, 0, 0, 0, 0, 0, 0, 0;  -- 占位，实际数据由应用层写入

-- =============================================================================
-- 8. 插入默认配置
-- =============================================================================

-- 插入匹配系统配置
INSERT INTO `gomoku_configs` (`config_name`, `config_data`) VALUES
('match_pool_config', JSON_OBJECT(
    'max_pool_size', 1000,
    'max_wait_seconds', 300,
    'match_interval_ms', 100,
    'initial_rating_range', 100,
    'rating_expansion_per_interval', 25,
    'max_rating_expansion', 500,
    'priority_wait_threshold_seconds', 30,
    'high_priority_expansion_bonus', 50,
    'excellent_quality_threshold', 50,
    'good_quality_threshold', 100,
    'fair_quality_threshold', 200,
    'rating_weight', 0.6,
    'wait_time_weight', 0.3,
    'tier_weight', 0.1
)) ON DUPLICATE KEY UPDATE
    config_data = VALUES(config_data);

-- =============================================================================
-- 完成信息
-- =============================================================================

SELECT
    'V003 Match Making System Migration Completed' as status,
    NOW() as completion_time,
    '新增表: match_records, match_pool_snapshots, match_daily_stats, player_match_history' as new_tables,
    '新增视图: v_recent_matches, v_player_match_stats' as new_views,
    '新增存储过程: SP_UpdateDailyMatchStats, SP_RecordMatch, SP_RecordPoolSnapshot' as new_procedures;
