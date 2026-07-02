/**
 * 游戏数据服务数据库迁移 - V002 修复脚本
 * 解决 MySQL 不支持 ALTER TABLE ADD COLUMN IF NOT EXISTS 的问题
 *
 * 此脚本是幂等的，可以安全地多次运行
 *
 * 使用方法：
 * 1. 先执行 V002_gameplay_system_extension.sql（会报错）
 * 2. 再执行本修复脚本
 * 或者直接执行本修复脚本（会处理所有情况）
 */

-- 注意：请确保在 game_service_db 数据库中执行此脚本
-- 在 phpMyAdmin 中选择 game_service_db 数据库后再执行

-- ==================== 0. 创建辅助存储过程 ====================

DELIMITER //

-- 删除已存在的辅助存储过程
DROP PROCEDURE IF EXISTS `AddColumnIfNotExists` //
DROP PROCEDURE IF EXISTS `AddIndexIfNotExists` //
DROP PROCEDURE IF EXISTS `DropIndexIfExists` //

/**
 * 安全添加列的存储过程
 */
CREATE PROCEDURE `AddColumnIfNotExists`(
    IN p_table_name VARCHAR(100),
    IN p_column_name VARCHAR(100),
    IN p_column_definition VARCHAR(1000)
)
BEGIN
    DECLARE column_exists INT DEFAULT 0;

    SELECT COUNT(*) INTO column_exists
    FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = p_table_name
      AND COLUMN_NAME = p_column_name;

    IF column_exists = 0 THEN
        SET @sql = CONCAT('ALTER TABLE `', p_table_name,
                         '` ADD COLUMN `', p_column_name, '` ', p_column_definition);
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;
        SELECT CONCAT('Added column: ', p_column_name) AS result;
    ELSE
        SELECT CONCAT('Column already exists: ', p_column_name) AS result;
    END IF;
END //

/**
 * 安全添加索引的存储过程
 */
CREATE PROCEDURE `AddIndexIfNotExists`(
    IN p_table_name VARCHAR(100),
    IN p_index_name VARCHAR(100),
    IN p_column_list VARCHAR(500)
)
BEGIN
    DECLARE index_exists INT DEFAULT 0;

    SELECT COUNT(*) INTO index_exists
    FROM information_schema.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = p_table_name
      AND INDEX_NAME = p_index_name;

    IF index_exists = 0 THEN
        SET @sql = CONCAT('CREATE INDEX `', p_index_name, '` ON `', p_table_name, '` (', p_column_list, ')');
        PREPARE stmt FROM @sql;
        EXECUTE stmt;
        DEALLOCATE PREPARE stmt;
        SELECT CONCAT('Added index: ', p_index_name) AS result;
    ELSE
        SELECT CONCAT('Index already exists: ', p_index_name) AS result;
    END IF;
END //

DELIMITER ;

-- ==================== 1. user_game_profiles 表扩展 ====================

-- 添加段位等级字段
CALL `AddColumnIfNotExists`('user_game_profiles', 'tier_level',
    "INT NOT NULL DEFAULT 3 COMMENT '段位等级 (0-15: 青铜III到大师)' AFTER `current_rating`");

-- 添加段位进度字段
CALL `AddColumnIfNotExists`('user_game_profiles', 'tier_progress',
    "INT NOT NULL DEFAULT 0 COMMENT '段位进度 (0-100)' AFTER `tier_level`");

-- 添加上一个段位字段
CALL `AddColumnIfNotExists`('user_game_profiles', 'previous_tier',
    "INT NULL DEFAULT NULL COMMENT '上一个段位' AFTER `tier_progress`");

-- 添加升段时间字段
CALL `AddColumnIfNotExists`('user_game_profiles', 'tier_promoted_at',
    "TIMESTAMP NULL DEFAULT NULL COMMENT '最近升段时间' AFTER `previous_tier`");

-- 添加索引
CALL `AddIndexIfNotExists`('user_game_profiles', 'idx_user_game_profiles_tier',
    '`game_type_id`, `tier_level`');

-- ==================== 2. 确保 user_daily_stats 表存在 ====================

CREATE TABLE IF NOT EXISTS `user_daily_stats` (
    `id` BIGINT NOT NULL AUTO_INCREMENT COMMENT '主键ID',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID',
    `game_type_id` INT NOT NULL COMMENT '游戏类型ID',
    `stat_date` DATE NOT NULL COMMENT '统计日期',
    `games_played` INT NOT NULL DEFAULT 0 COMMENT '当日游戏场次',
    `games_won` INT NOT NULL DEFAULT 0 COMMENT '当日胜场',
    `games_lost` INT NOT NULL DEFAULT 0 COMMENT '当日败场',
    `games_drawn` INT NOT NULL DEFAULT 0 COMMENT '当日平局',
    `first_win_at` TIMESTAMP NULL DEFAULT NULL COMMENT '首胜时间',
    `first_win_claimed` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '首胜奖励是否已领取',
    `total_playtime_seconds` INT NOT NULL DEFAULT 0 COMMENT '当日总游戏时长(秒)',
    `total_gold_earned` INT NOT NULL DEFAULT 0 COMMENT '当日获得金币',
    `total_honor_earned` INT NOT NULL DEFAULT 0 COMMENT '当日获得荣誉',
    `total_exp_earned` INT NOT NULL DEFAULT 0 COMMENT '当日获得经验',
    `rating_change` INT NOT NULL DEFAULT 0 COMMENT '当日评分变化',
    `peak_streak` INT NOT NULL DEFAULT 0 COMMENT '当日最高连胜',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_user_daily_stats_user_game_date` (`user_id`, `game_type_id`, `stat_date`),
    KEY `idx_user_daily_stats_date` (`stat_date`),
    KEY `idx_user_daily_stats_user_date` (`user_id`, `stat_date`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户每日统计表';

-- ==================== 3. 确保 tier_history 表存在 ====================

CREATE TABLE IF NOT EXISTS `tier_history` (
    `id` BIGINT NOT NULL AUTO_INCREMENT COMMENT '主键ID',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID',
    `game_type_id` INT NOT NULL COMMENT '游戏类型ID',
    `tier_before` INT NOT NULL COMMENT '变化前段位',
    `tier_after` INT NOT NULL COMMENT '变化后段位',
    `rating_before` INT NOT NULL COMMENT '变化前评分',
    `rating_after` INT NOT NULL COMMENT '变化后评分',
    `is_promotion` BOOLEAN NOT NULL COMMENT '是否升级',
    `game_id` VARCHAR(128) NULL DEFAULT NULL COMMENT '触发变化的游戏ID',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    KEY `idx_tier_history_user` (`user_id`, `game_type_id`),
    KEY `idx_tier_history_created_at` (`created_at`),
    KEY `idx_tier_history_promotion` (`is_promotion`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='段位历史表';

-- ==================== 4. 确保 reward_logs 表存在 ====================

CREATE TABLE IF NOT EXISTS `reward_logs` (
    `id` BIGINT NOT NULL AUTO_INCREMENT COMMENT '主键ID',
    `log_id` VARCHAR(128) NOT NULL COMMENT '日志唯一ID',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID',
    `game_type_id` INT NOT NULL COMMENT '游戏类型ID',
    `game_id` VARCHAR(128) NULL DEFAULT NULL COMMENT '游戏ID',
    `reward_type` VARCHAR(50) NOT NULL COMMENT '奖励类型 (game_end/daily_first_win/streak_bonus/event)',
    `currency_type` VARCHAR(20) NULL DEFAULT NULL COMMENT '货币类型 (GOLD/GEM/HONOR)',
    `currency_amount` INT NOT NULL DEFAULT 0 COMMENT '货币数量',
    `experience` INT NOT NULL DEFAULT 0 COMMENT '经验值',
    `rating_change` INT NOT NULL DEFAULT 0 COMMENT '评分变化',
    `reason` VARCHAR(255) NULL DEFAULT NULL COMMENT '奖励原因',
    `metadata` JSON DEFAULT NULL COMMENT '额外元数据',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_reward_logs_log_id` (`log_id`),
    KEY `idx_reward_logs_user` (`user_id`),
    KEY `idx_reward_logs_game` (`game_id`),
    KEY `idx_reward_logs_type` (`reward_type`),
    KEY `idx_reward_logs_created_at` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='奖励发放日志表';

-- ==================== 5. 确保 game_settlements 表存在 ====================

CREATE TABLE IF NOT EXISTS `game_settlements` (
    `id` BIGINT NOT NULL AUTO_INCREMENT COMMENT '主键ID',
    `settlement_id` VARCHAR(128) NOT NULL COMMENT '结算ID',
    `game_id` VARCHAR(128) NOT NULL COMMENT '游戏ID',
    `game_type` VARCHAR(50) NOT NULL COMMENT '游戏类型 (gomoku/chess/etc)',
    `game_type_id` INT NOT NULL COMMENT '游戏类型ID',
    `game_mode` VARCHAR(50) NOT NULL COMMENT '游戏模式 (casual/ranked/tournament)',
    `duration_seconds` INT NOT NULL DEFAULT 0 COMMENT '游戏时长(秒)',
    `started_at` TIMESTAMP NOT NULL COMMENT '开始时间',
    `ended_at` TIMESTAMP NOT NULL COMMENT '结束时间',
    `player_count` INT NOT NULL DEFAULT 0 COMMENT '玩家数量',
    `settlement_data` JSON NOT NULL COMMENT '完整结算数据JSON',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_game_settlements_settlement_id` (`settlement_id`),
    UNIQUE KEY `uk_game_settlements_game_id` (`game_id`),
    KEY `idx_game_settlements_game_type` (`game_type`),
    KEY `idx_game_settlements_mode` (`game_mode`),
    KEY `idx_game_settlements_ended_at` (`ended_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='游戏结算详情表';

-- ==================== 6. 确保 game_settlement_players 表存在 ====================

CREATE TABLE IF NOT EXISTS `game_settlement_players` (
    `id` BIGINT NOT NULL AUTO_INCREMENT COMMENT '主键ID',
    `settlement_id` VARCHAR(128) NOT NULL COMMENT '结算ID',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID',
    `result` VARCHAR(20) NOT NULL COMMENT '游戏结果 (win/loss/draw/surrender/timeout)',
    `rating_before` INT NOT NULL COMMENT '评分变化前',
    `rating_after` INT NOT NULL COMMENT '评分变化后',
    `rating_change` INT NOT NULL COMMENT '评分变化量',
    `tier_before` INT NOT NULL COMMENT '段位变化前',
    `tier_after` INT NOT NULL COMMENT '段位变化后',
    `tier_changed` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '段位是否变化',
    `gold_earned` INT NOT NULL DEFAULT 0 COMMENT '获得金币',
    `gem_earned` INT NOT NULL DEFAULT 0 COMMENT '获得宝石',
    `honor_earned` INT NOT NULL DEFAULT 0 COMMENT '获得荣誉',
    `experience_earned` INT NOT NULL DEFAULT 0 COMMENT '获得经验',
    `win_streak_before` INT NOT NULL DEFAULT 0 COMMENT '连胜变化前',
    `win_streak_after` INT NOT NULL DEFAULT 0 COMMENT '连胜变化后',
    `is_first_win_today` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '是否今日首胜',
    `achievements_unlocked` JSON DEFAULT NULL COMMENT '解锁的成就列表',
    `bonuses` JSON DEFAULT NULL COMMENT '额外奖励信息',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_game_settlement_players_settlement_user` (`settlement_id`, `user_id`),
    KEY `idx_game_settlement_players_user` (`user_id`),
    KEY `idx_game_settlement_players_result` (`result`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='游戏结算玩家表';

-- ==================== 7. 更新/创建存储过程 ====================

DELIMITER //

-- 更新用户游戏统计存储过程
DROP PROCEDURE IF EXISTS `UpdateUserGameStats` //

CREATE PROCEDURE `UpdateUserGameStats`(
    IN p_user_id VARCHAR(64),
    IN p_game_type_id INT,
    IN p_result VARCHAR(20),
    IN p_rating_change INT,
    IN p_duration_seconds INT,
    IN p_gold_earned INT,
    IN p_honor_earned INT,
    IN p_exp_earned INT,
    IN p_new_rating INT,
    IN p_new_tier INT,
    IN p_is_first_win BOOLEAN,
    IN p_game_id VARCHAR(128)
)
BEGIN
    DECLARE v_today DATE DEFAULT CURRENT_DATE();
    DECLARE v_tier_changed INT DEFAULT 0;
    DECLARE v_old_tier INT DEFAULT 3;

    -- 获取旧段位（如果列存在）
    SELECT IFNULL(`tier_level`, 3) INTO v_old_tier
    FROM `user_game_profiles`
    WHERE `user_id` = p_user_id AND `game_type_id` = p_game_type_id
    LIMIT 1;

    -- 更新用户游戏档案
    UPDATE `user_game_profiles`
    SET
        `current_rating` = p_new_rating,
        `peak_rating` = GREATEST(IFNULL(`peak_rating`, 1200), p_new_rating),
        `tier_level` = p_new_tier,
        `total_games` = `total_games` + 1,
        `wins` = `wins` + IF(p_result = 'win', 1, 0),
        `losses` = `losses` + IF(p_result IN ('loss', 'surrender', 'timeout'), 1, 0),
        `draws` = `draws` + IF(p_result = 'draw', 1, 0),
        `current_win_streak` = IF(p_result = 'win', `current_win_streak` + 1, 0),
        `best_win_streak` = GREATEST(IFNULL(`best_win_streak`, 0), IF(p_result = 'win', `current_win_streak` + 1, `best_win_streak`)),
        `total_playtime_seconds` = `total_playtime_seconds` + p_duration_seconds,
        `last_played_at` = NOW()
    WHERE `user_id` = p_user_id AND `game_type_id` = p_game_type_id;

    -- 更新每日统计
    INSERT INTO `user_daily_stats` (
        `user_id`, `game_type_id`, `stat_date`,
        `games_played`, `games_won`, `games_lost`, `games_drawn`,
        `first_win_at`, `total_playtime_seconds`,
        `total_gold_earned`, `total_honor_earned`, `total_exp_earned`, `rating_change`
    ) VALUES (
        p_user_id, p_game_type_id, v_today,
        1, IF(p_result = 'win', 1, 0), IF(p_result IN ('loss', 'surrender', 'timeout'), 1, 0), IF(p_result = 'draw', 1, 0),
        IF(p_is_first_win, NOW(), NULL),
        p_duration_seconds,
        p_gold_earned, p_honor_earned, p_exp_earned, p_rating_change
    )
    ON DUPLICATE KEY UPDATE
        `games_played` = `games_played` + 1,
        `games_won` = `games_won` + IF(p_result = 'win', 1, 0),
        `games_lost` = `games_lost` + IF(p_result IN ('loss', 'surrender', 'timeout'), 1, 0),
        `games_drawn` = `games_drawn` + IF(p_result = 'draw', 1, 0),
        `first_win_at` = IF(p_is_first_win AND `first_win_at` IS NULL, NOW(), `first_win_at`),
        `total_playtime_seconds` = `total_playtime_seconds` + p_duration_seconds,
        `total_gold_earned` = `total_gold_earned` + p_gold_earned,
        `total_honor_earned` = `total_honor_earned` + p_honor_earned,
        `total_exp_earned` = `total_exp_earned` + p_exp_earned,
        `rating_change` = `rating_change` + p_rating_change;

    -- 记录段位变化
    IF v_old_tier != p_new_tier THEN
        INSERT INTO `tier_history` (
            `user_id`, `game_type_id`, `tier_before`, `tier_after`,
            `rating_before`, `rating_after`, `is_promotion`, `game_id`
        ) VALUES (
            p_user_id, p_game_type_id, v_old_tier, p_new_tier,
            p_new_rating - p_rating_change, p_new_rating,
            p_new_tier > v_old_tier, p_game_id
        );
    END IF;

END //

-- 获取用户今日统计存储过程
DROP PROCEDURE IF EXISTS `GetUserDailyStats` //

CREATE PROCEDURE `GetUserDailyStats`(
    IN p_user_id VARCHAR(64),
    IN p_game_type_id INT
)
BEGIN
    SELECT * FROM `user_daily_stats`
    WHERE `user_id` = p_user_id
    AND `game_type_id` = p_game_type_id
    AND `stat_date` = CURRENT_DATE();
END //

-- 清理过期每日统计存储过程
DROP PROCEDURE IF EXISTS `CleanupOldDailyStats` //

CREATE PROCEDURE `CleanupOldDailyStats`()
BEGIN
    DELETE FROM `user_daily_stats`
    WHERE `stat_date` < DATE_SUB(CURRENT_DATE(), INTERVAL 90 DAY);

    SELECT ROW_COUNT() AS deleted_rows;
END //

DELIMITER ;

-- ==================== 8. 根据评分更新段位等级 ====================

-- 仅更新 tier_level 为默认值(3)或 NULL 的记录
UPDATE `user_game_profiles`
SET `tier_level` = CASE
    WHEN `current_rating` < 400 THEN 0   -- 青铜 III
    WHEN `current_rating` < 600 THEN 1   -- 青铜 II
    WHEN `current_rating` < 800 THEN 2   -- 青铜 I
    WHEN `current_rating` < 1000 THEN 3  -- 白银 III
    WHEN `current_rating` < 1200 THEN 4  -- 白银 II
    WHEN `current_rating` < 1400 THEN 5  -- 白银 I
    WHEN `current_rating` < 1600 THEN 6  -- 黄金 III
    WHEN `current_rating` < 1800 THEN 7  -- 黄金 II
    WHEN `current_rating` < 2000 THEN 8  -- 黄金 I
    WHEN `current_rating` < 2200 THEN 9  -- 铂金 III
    WHEN `current_rating` < 2400 THEN 10 -- 铂金 II
    WHEN `current_rating` < 2600 THEN 11 -- 铂金 I
    WHEN `current_rating` < 2800 THEN 12 -- 钻石 III
    WHEN `current_rating` < 2900 THEN 13 -- 钻石 II
    WHEN `current_rating` < 3000 THEN 14 -- 钻石 I
    ELSE 15                               -- 大师
END
WHERE `tier_level` = 3 OR `tier_level` IS NULL;

-- ==================== 9. 记录迁移版本 ====================


-- ==================== 10. 验证结果 ====================


SELECT '========== V002 修复完成! ==========' AS status;
