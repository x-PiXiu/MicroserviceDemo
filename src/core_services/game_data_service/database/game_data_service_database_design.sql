/*
 * 游戏数据服务数据库设计 - 符合推荐分层架构
 * 数据库名: game_service_db
 * 描述: 管理跨游戏数据，包括用户游戏档案、成就、库存和货币
 * 作者: AI Assistant
 * 日期: 2025-09-19
 * 版本: 1.0.0
 */

-- 创建游戏数据服务数据库
CREATE DATABASE IF NOT EXISTS `game_service_db` 
DEFAULT CHARACTER SET utf8mb4 
DEFAULT COLLATE utf8mb4_general_ci;

USE `game_service_db`;

-- ==================== 用户游戏档案表 ====================

/*
 * 用户游戏档案表 (user_game_profiles)
 * 存储用户在特定游戏中的档案信息
 */
CREATE TABLE IF NOT EXISTS `user_game_profiles` (
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
    `game_type_id` INT NOT NULL COMMENT '游戏类型ID (1:五子棋 2:贪吃蛇 等)',
    `level` INT NOT NULL DEFAULT 1 COMMENT '用户等级',
    `experience_points` INT NOT NULL DEFAULT 0 COMMENT '经验值',
    `current_rating` INT NOT NULL DEFAULT 1200 COMMENT '当前评分',
    `peak_rating` INT NOT NULL DEFAULT 1200 COMMENT '历史最高评分',
    `total_games` INT NOT NULL DEFAULT 0 COMMENT '总游戏场次',
    `wins` INT NOT NULL DEFAULT 0 COMMENT '获胜场次',
    `losses` INT NOT NULL DEFAULT 0 COMMENT '失败场次',
    `draws` INT NOT NULL DEFAULT 0 COMMENT '平局场次',
    `current_win_streak` INT NOT NULL DEFAULT 0 COMMENT '当前连胜',
    `best_win_streak` INT NOT NULL DEFAULT 0 COMMENT '最佳连胜',
    `total_playtime_seconds` INT NOT NULL DEFAULT 0 COMMENT '总游戏时间(秒)',
    `current_season` VARCHAR(50) NOT NULL DEFAULT 'season_2025_1' COMMENT '当前赛季',
    `season_rating` INT NOT NULL DEFAULT 1200 COMMENT '赛季评分',
    `season_games` INT NOT NULL DEFAULT 0 COMMENT '赛季场次',
    `profile_data` JSON DEFAULT NULL COMMENT '扩展档案数据JSON',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `last_played_at` TIMESTAMP NULL DEFAULT NULL COMMENT '最后游戏时间',
    PRIMARY KEY (`user_id`, `game_type_id`),
    KEY `idx_user_game_profiles_game_type` (`game_type_id`),
    KEY `idx_user_game_profiles_rating` (`current_rating`),
    KEY `idx_user_game_profiles_level` (`level`),
    KEY `idx_user_game_profiles_last_played` (`last_played_at`),
    KEY `idx_user_game_profiles_season` (`current_season`, `season_rating`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户游戏档案表';

-- ==================== 成就系统表 ====================

/*
 * 成就定义表 (achievement_definitions)
 * 存储游戏成就的定义信息
 */
CREATE TABLE IF NOT EXISTS `achievement_definitions` (
    `achievement_id` VARCHAR(128) NOT NULL COMMENT '成就ID (主键)',
    `game_type_id` INT NOT NULL COMMENT '游戏类型ID',
    `name` VARCHAR(255) NOT NULL COMMENT '成就名称',
    `description` TEXT NOT NULL COMMENT '成就描述',
    `icon_url` TEXT DEFAULT NULL COMMENT '图标URL',
    `achievement_type` VARCHAR(50) NOT NULL DEFAULT 'progress' COMMENT '成就类型 (progress/milestone/hidden)',
    `max_progress` INT NOT NULL DEFAULT 1 COMMENT '最大进度值',
    `requirements` JSON DEFAULT NULL COMMENT '解锁条件JSON',
    `rewards` JSON DEFAULT NULL COMMENT '奖励内容JSON',
    `points` INT NOT NULL DEFAULT 10 COMMENT '成就点数',
    `rarity` VARCHAR(20) NOT NULL DEFAULT 'common' COMMENT '稀有度 (common/rare/epic/legendary)',
    `is_hidden` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '是否隐藏成就',
    `is_active` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '是否活跃',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`achievement_id`),
    KEY `idx_achievement_definitions_game_type` (`game_type_id`),
    KEY `idx_achievement_definitions_type` (`achievement_type`),
    KEY `idx_achievement_definitions_rarity` (`rarity`),
    KEY `idx_achievement_definitions_active` (`is_active`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='成就定义表';

/*
 * 用户成就表 (user_achievements)
 * 存储用户获得的成就信息
 */
CREATE TABLE IF NOT EXISTS `user_achievements` (
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
    `achievement_id` VARCHAR(128) NOT NULL COMMENT '成就ID (外键)',
    `current_progress` INT NOT NULL DEFAULT 0 COMMENT '当前进度',
    `is_unlocked` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '是否已解锁',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `unlocked_at` TIMESTAMP NULL DEFAULT NULL COMMENT '解锁时间',
    PRIMARY KEY (`user_id`, `achievement_id`),
    KEY `idx_user_achievements_unlocked` (`is_unlocked`),
    KEY `idx_user_achievements_unlocked_at` (`unlocked_at`),
    CONSTRAINT `fk_user_achievements_achievement_id` FOREIGN KEY (`achievement_id`) REFERENCES `achievement_definitions` (`achievement_id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户成就表';

-- ==================== 库存系统表 ====================

/*
 * 物品定义表 (item_definitions)
 * 存储游戏物品的定义信息
 */
CREATE TABLE IF NOT EXISTS `item_definitions` (
    `item_id` VARCHAR(128) NOT NULL COMMENT '物品ID (主键)',
    `game_type_id` INT NOT NULL COMMENT '游戏类型ID',
    `name` VARCHAR(255) NOT NULL COMMENT '物品名称',
    `description` TEXT NOT NULL COMMENT '物品描述',
    `icon_url` TEXT DEFAULT NULL COMMENT '图标URL',
    `item_type` VARCHAR(50) NOT NULL COMMENT '物品类型 (consumable/equipment/material)',
    `rarity` VARCHAR(20) NOT NULL DEFAULT 'common' COMMENT '稀有度',
    `max_stack` INT NOT NULL DEFAULT 1 COMMENT '最大堆叠数量',
    `is_tradable` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '是否可交易',
    `properties` JSON DEFAULT NULL COMMENT '物品属性JSON',
    `effects` JSON DEFAULT NULL COMMENT '物品效果JSON',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`item_id`),
    KEY `idx_item_definitions_game_type` (`game_type_id`),
    KEY `idx_item_definitions_type` (`item_type`),
    KEY `idx_item_definitions_rarity` (`rarity`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='物品定义表';

/*
 * 用户库存表 (user_inventory)
 * 存储用户拥有的物品信息
 */
CREATE TABLE IF NOT EXISTS `user_inventory` (
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
    `item_id` VARCHAR(128) NOT NULL COMMENT '物品ID (外键)',
    `quantity` INT NOT NULL DEFAULT 0 COMMENT '数量',
    `item_data` JSON DEFAULT NULL COMMENT '物品数据JSON (如随机属性)',
    `acquired_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '获得时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `expires_at` TIMESTAMP NULL DEFAULT NULL COMMENT '过期时间',
    PRIMARY KEY (`user_id`, `item_id`),
    KEY `idx_user_inventory_quantity` (`quantity`),
    KEY `idx_user_inventory_acquired_at` (`acquired_at`),
    KEY `idx_user_inventory_expires_at` (`expires_at`),
    CONSTRAINT `fk_user_inventory_item_id` FOREIGN KEY (`item_id`) REFERENCES `item_definitions` (`item_id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户库存表';

-- ==================== 货币系统表 ====================

/*
 * 货币定义表 (currency_definitions)
 * 存储游戏货币的定义信息
 */
CREATE TABLE IF NOT EXISTS `currency_definitions` (
    `currency_type_id` INT NOT NULL AUTO_INCREMENT COMMENT '货币类型ID (主键)',
    `name` VARCHAR(255) NOT NULL COMMENT '货币名称',
    `description` TEXT NOT NULL COMMENT '货币描述',
    `icon_url` TEXT DEFAULT NULL COMMENT '图标URL',
    `currency_code` VARCHAR(20) NOT NULL COMMENT '货币代码 (如 GOLD, GEM)',
    `max_amount` INT NOT NULL DEFAULT 999999999 COMMENT '最大持有量',
    `is_premium` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '是否为付费货币',
    `is_transferable` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '是否可转账',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`currency_type_id`),
    UNIQUE KEY `uk_currency_definitions_code` (`currency_code`),
    KEY `idx_currency_definitions_premium` (`is_premium`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='货币定义表';

/*
 * 用户货币表 (user_currency)
 * 存储用户拥有的货币信息
 */
CREATE TABLE IF NOT EXISTS `user_currency` (
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
    `currency_type_id` INT NOT NULL COMMENT '货币类型ID (外键)',
    `amount` INT NOT NULL DEFAULT 0 COMMENT '当前数量',
    `total_earned` INT NOT NULL DEFAULT 0 COMMENT '总获得数量',
    `total_spent` INT NOT NULL DEFAULT 0 COMMENT '总消费数量',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`user_id`, `currency_type_id`),
    KEY `idx_user_currency_amount` (`amount`),
    CONSTRAINT `fk_user_currency_type_id` FOREIGN KEY (`currency_type_id`) REFERENCES `currency_definitions` (`currency_type_id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户货币表';

/*
 * 货币交易表 (currency_transactions)
 * 存储货币变动记录
 */
CREATE TABLE IF NOT EXISTS `currency_transactions` (
    `transaction_id` VARCHAR(128) NOT NULL COMMENT '交易ID (主键)',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
    `currency_type_id` INT NOT NULL COMMENT '货币类型ID (外键)',
    `transaction_type` VARCHAR(50) NOT NULL COMMENT '交易类型 (earn/spend/transfer)',
    `amount` INT NOT NULL COMMENT '交易金额',
    `balance_before` INT NOT NULL COMMENT '交易前余额',
    `balance_after` INT NOT NULL COMMENT '交易后余额',
    `reason` VARCHAR(255) NOT NULL COMMENT '交易原因',
    `source` VARCHAR(100) NOT NULL COMMENT '来源 (game_reward/purchase/admin)',
    `metadata` JSON DEFAULT NULL COMMENT '元数据JSON',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`transaction_id`),
    KEY `idx_currency_transactions_user_id` (`user_id`),
    KEY `idx_currency_transactions_type` (`transaction_type`),
    KEY `idx_currency_transactions_created_at` (`created_at`),
    CONSTRAINT `fk_currency_transactions_currency_type_id` FOREIGN KEY (`currency_type_id`) REFERENCES `currency_definitions` (`currency_type_id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='货币交易表';

-- ==================== 游戏结果表 ====================

/*
 * 游戏结果表 (game_results)
 * 存储游戏结果记录
 */
CREATE TABLE IF NOT EXISTS `game_results` (
    `result_id` VARCHAR(128) NOT NULL COMMENT '结果ID (主键)',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
    `game_type_id` INT NOT NULL COMMENT '游戏类型ID',
    `session_id` VARCHAR(128) NOT NULL COMMENT '游戏会话ID',
    `result` VARCHAR(20) NOT NULL COMMENT '游戏结果 (win/loss/draw)',
    `score` INT NOT NULL DEFAULT 0 COMMENT '得分',
    `rating_change` INT NOT NULL DEFAULT 0 COMMENT '评分变化',
    `experience_gained` INT NOT NULL DEFAULT 0 COMMENT '获得经验',
    `duration_seconds` INT NOT NULL DEFAULT 0 COMMENT '游戏时长(秒)',
    `moves_count` INT NOT NULL DEFAULT 0 COMMENT '移动/操作次数',
    `game_data` JSON DEFAULT NULL COMMENT '游戏详细数据JSON',
    `rewards` JSON DEFAULT NULL COMMENT '奖励内容JSON',
    `opponent_id` VARCHAR(64) DEFAULT NULL COMMENT '对手ID (如果有)',
    `played_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '游戏时间',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '记录创建时间',
    PRIMARY KEY (`result_id`),
    KEY `idx_game_results_user_id` (`user_id`),
    KEY `idx_game_results_game_type` (`game_type_id`),
    KEY `idx_game_results_result` (`result`),
    KEY `idx_game_results_played_at` (`played_at`),
    KEY `idx_game_results_session_id` (`session_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='游戏结果表';

-- ==================== 排行榜表 ====================

/*
 * 排行榜表 (leaderboards)
 * 存储排行榜数据
 */
CREATE TABLE IF NOT EXISTS `leaderboards` (
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID',
    `game_type_id` INT NOT NULL COMMENT '游戏类型ID',
    `leaderboard_type` VARCHAR(50) NOT NULL COMMENT '排行榜类型 (rating/wins/playtime)',
    `rank` INT NOT NULL COMMENT '排名',
    `score` INT NOT NULL COMMENT '分数',
    `username` VARCHAR(50) NOT NULL COMMENT '用户名 (冗余存储便于查询)',
    `nickname` VARCHAR(100) NOT NULL COMMENT '昵称',
    `avatar_url` TEXT DEFAULT NULL COMMENT '头像URL',
    `extra_data` JSON DEFAULT NULL COMMENT '额外数据JSON',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`user_id`, `game_type_id`, `leaderboard_type`),
    KEY `idx_leaderboards_game_type_type_rank` (`game_type_id`, `leaderboard_type`, `rank`),
    KEY `idx_leaderboards_score` (`score`),
    KEY `idx_leaderboards_updated_at` (`updated_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='排行榜表';

-- ==================== 初始化数据 ====================

-- 插入默认货币定义
INSERT IGNORE INTO `currency_definitions` (
    `currency_type_id`, `name`, `description`, `currency_code`, `max_amount`, `is_premium`
) VALUES 
(1, '金币', '游戏内基础货币', 'GOLD', 999999999, FALSE),
(2, '宝石', '游戏内高级货币', 'GEM', 99999, TRUE),
(3, '荣誉点', '竞技场荣誉货币', 'HONOR', 999999, FALSE);

-- 插入五子棋成就定义
INSERT IGNORE INTO `achievement_definitions` (
    `achievement_id`, `game_type_id`, `name`, `description`, `achievement_type`, `max_progress`, `points`, `rarity`
) VALUES 
('gomoku_first_win', 1, '首胜', '赢得你的第一场五子棋游戏', 'milestone', 1, 10, 'common'),
('gomoku_total_games_10', 1, '游戏达人', '完成10场五子棋游戏', 'progress', 10, 20, 'common'),
('gomoku_total_games_100', 1, '游戏专家', '完成100场五子棋游戏', 'progress', 100, 50, 'rare'),
('gomoku_win_streak_5', 1, '连胜高手', '五子棋连胜5场', 'progress', 5, 30, 'rare'),
('gomoku_rating_1500', 1, '高级玩家', '五子棋评分达到1500', 'milestone', 1, 40, 'epic'),
('gomoku_playtime_10h', 1, '时间大师', '五子棋总游戏时间达到10小时', 'progress', 36000, 25, 'common');

-- 插入基础物品定义
INSERT IGNORE INTO `item_definitions` (
    `item_id`, `game_type_id`, `name`, `description`, `item_type`, `rarity`, `max_stack`
) VALUES 
('exp_boost_small', 0, '小经验加成卡', '增加20%经验获得，持续1小时', 'consumable', 'common', 10),
('exp_boost_large', 0, '大经验加成卡', '增加50%经验获得，持续1小时', 'consumable', 'rare', 5),
('rating_protection', 0, '评分保护卡', '保护一次评分下降', 'consumable', 'epic', 3),
('title_winner', 1, '五子棋胜者', '五子棋专用称号', 'equipment', 'rare', 1);

-- ==================== 存储过程 ====================

DELIMITER //

/*
 * 存储过程：创建用户游戏档案
 */
CREATE PROCEDURE `CreateUserGameProfile`(
    IN p_user_id VARCHAR(64),
    IN p_game_type_id INT,
    IN p_initial_rating INT
)
BEGIN
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;

    START TRANSACTION;
    
    -- 创建游戏档案
    INSERT INTO `user_game_profiles` (
        `user_id`, `game_type_id`, `current_rating`, `peak_rating`, `season_rating`
    ) VALUES (
        p_user_id, p_game_type_id, p_initial_rating, p_initial_rating, p_initial_rating
    );
    
    -- 初始化基础货币
    INSERT IGNORE INTO `user_currency` (
        `user_id`, `currency_type_id`, `amount`
    ) VALUES 
    (p_user_id, 1, 1000),  -- 1000金币
    (p_user_id, 2, 10),    -- 10宝石
    (p_user_id, 3, 0);     -- 0荣誉点
    
    -- 创建基础成就记录
    INSERT IGNORE INTO `user_achievements` (
        `user_id`, `achievement_id`
    ) 
    SELECT p_user_id, `achievement_id` 
    FROM `achievement_definitions` 
    WHERE `game_type_id` = p_game_type_id OR `game_type_id` = 0;
    
    COMMIT;
END //

/*
 * 存储过程：更新排行榜
 */
CREATE PROCEDURE `UpdateLeaderboard`(
    IN p_game_type_id INT,
    IN p_leaderboard_type VARCHAR(50)
)
BEGIN
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;

    START TRANSACTION;
    
    -- 删除旧排行榜数据
    DELETE FROM `leaderboards` 
    WHERE `game_type_id` = p_game_type_id 
    AND `leaderboard_type` = p_leaderboard_type;
    
    -- 根据类型重新计算排行榜
    IF p_leaderboard_type = 'rating' THEN
        INSERT INTO `leaderboards` (
            `user_id`, `game_type_id`, `leaderboard_type`, `rank`, `score`, 
            `username`, `nickname`, `avatar_url`
        )
        SELECT 
            ugp.`user_id`, 
            ugp.`game_type_id`,
            'rating',
            ROW_NUMBER() OVER (ORDER BY ugp.`current_rating` DESC),
            ugp.`current_rating`,
            'unknown', -- 需要从用户服务获取
            'unknown',
            ''
        FROM `user_game_profiles` ugp
        WHERE ugp.`game_type_id` = p_game_type_id
        ORDER BY ugp.`current_rating` DESC
        LIMIT 1000;
    END IF;
    
    COMMIT;
END //

DELIMITER ;

-- ==================== 索引优化建议 ====================

/*
 * 根据实际使用情况，可能需要添加的额外索引：
 * 
 * -- 复合索引：用户游戏档案查询优化
 * CREATE INDEX `idx_user_game_profiles_user_rating` ON `user_game_profiles` (`user_id`, `current_rating`);
 * 
 * -- 复合索引：成就查询优化
 * CREATE INDEX `idx_user_achievements_user_game` ON `user_achievements` (`user_id`, `achievement_id`, `is_unlocked`);
 * 
 * -- 分区表：游戏结果按月分区
 * ALTER TABLE `game_results` PARTITION BY RANGE (MONTH(`played_at`)) (
 *     PARTITION p202501 VALUES LESS THAN (2),
 *     PARTITION p202502 VALUES LESS THAN (3),
 *     ...
 * );
 */

-- ==================== 权限设置 ====================

-- 创建游戏数据服务专用数据库用户
-- CREATE USER IF NOT EXISTS 'game_data_service'@'localhost' IDENTIFIED BY 'game_data_pass123';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON `game_service_db`.* TO 'game_data_service'@'localhost';
-- FLUSH PRIVILEGES;

-- 显示表结构
SHOW TABLES;




