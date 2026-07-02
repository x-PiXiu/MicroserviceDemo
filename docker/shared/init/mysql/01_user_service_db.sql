/*
 * 用户核心服务数据库设计 - 符合推荐分层架构
 * 数据库名: user_service_db
 * 描述: 管理用户核心数据，包括用户基础信息、档案、偏好设置和会话
 * 作者: AI Assistant
 * 日期: 2025-09-19
 * 版本: 1.0.0
 */

-- 创建用户核心服务数据库
CREATE DATABASE IF NOT EXISTS `user_service_db` 
DEFAULT CHARACTER SET utf8mb4 
DEFAULT COLLATE utf8mb4_general_ci;

USE `user_service_db`;

-- ==================== 用户基础信息表 ====================

/*
 * 用户基础信息表 (users)
 * 存储用户核心登录信息和状态
 */
CREATE TABLE IF NOT EXISTS `users` (
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (主键)',
    `username` VARCHAR(50) NOT NULL COMMENT '用户名 (唯一)',
    `email` VARCHAR(255) NOT NULL COMMENT '邮箱 (唯一)',
    `password_hash` VARCHAR(255) NOT NULL COMMENT '密码哈希',
    `salt` VARCHAR(64) NOT NULL COMMENT '密码盐值',
    `phone` VARCHAR(20) DEFAULT NULL COMMENT '手机号',
    `status` TINYINT NOT NULL DEFAULT 1 COMMENT '用户状态 (0:未激活 1:活跃 2:暂停 3:封禁 4:已删除)',
    `online_status` TINYINT NOT NULL DEFAULT 0 COMMENT '在线状态 (0:离线 1:在线 2:离开 3:忙碌)',
    `login_attempts` INT NOT NULL DEFAULT 0 COMMENT '登录失败次数',
    `locked_until` TIMESTAMP NULL DEFAULT NULL COMMENT '锁定截止时间',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `last_login_at` TIMESTAMP NULL DEFAULT NULL COMMENT '最后登录时间',
    `last_login_ip` VARCHAR(45) DEFAULT NULL COMMENT '最后登录IP',
    PRIMARY KEY (`user_id`),
    UNIQUE KEY `uk_users_username` (`username`),
    UNIQUE KEY `uk_users_email` (`email`),
    KEY `idx_users_status` (`status`),
    KEY `idx_users_online_status` (`online_status`),
    KEY `idx_users_created_at` (`created_at`),
    KEY `idx_users_last_login_at` (`last_login_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户基础信息表';

-- ==================== 用户档案表 ====================

/*
 * 用户档案表 (user_profiles)
 * 存储用户扩展信息和个人资料
 */
CREATE TABLE IF NOT EXISTS `user_profiles` (
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
    `nickname` VARCHAR(100) NOT NULL COMMENT '昵称',
    `real_name` VARCHAR(100) DEFAULT NULL COMMENT '真实姓名',
    `avatar_url` TEXT DEFAULT NULL COMMENT '头像URL',
    `bio` TEXT DEFAULT NULL COMMENT '个人简介',
    `location` VARCHAR(255) DEFAULT NULL COMMENT '地理位置',
    `website` VARCHAR(500) DEFAULT NULL COMMENT '个人网站',
    `birth_date` DATE DEFAULT NULL COMMENT '生日',
    `gender` CHAR(1) DEFAULT NULL COMMENT '性别 (M:男 F:女 O:其他)',
    `language` VARCHAR(10) NOT NULL DEFAULT 'zh-CN' COMMENT '首选语言',
    `timezone` VARCHAR(50) NOT NULL DEFAULT 'Asia/Shanghai' COMMENT '时区',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`user_id`),
    KEY `idx_user_profiles_nickname` (`nickname`),
    KEY `idx_user_profiles_location` (`location`),
    KEY `idx_user_profiles_created_at` (`created_at`),
    CONSTRAINT `fk_user_profiles_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`user_id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户档案表';

-- ==================== 用户偏好设置表 ====================

/*
 * 用户偏好设置表 (user_preferences)
 * 存储用户个性化设置和偏好
 */
CREATE TABLE IF NOT EXISTS `user_preferences` (
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
    `email_notifications` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '邮件通知',
    `push_notifications` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '推送通知',
    `sms_notifications` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '短信通知',
    `theme` VARCHAR(20) NOT NULL DEFAULT 'light' COMMENT '主题 (light/dark)',
    `language` VARCHAR(10) NOT NULL DEFAULT 'zh-CN' COMMENT '界面语言',
    `sound_effects` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '音效开关',
    `music` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '音乐开关',
    `volume` TINYINT NOT NULL DEFAULT 80 COMMENT '音量 (0-100)',
    `auto_login` BOOLEAN NOT NULL DEFAULT FALSE COMMENT '自动登录',
    `remember_me` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '记住我',
    `session_timeout` INT NOT NULL DEFAULT 3600 COMMENT '会话超时时间(秒)',
    `custom_settings` JSON DEFAULT NULL COMMENT '自定义设置JSON',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`user_id`),
    KEY `idx_user_preferences_theme` (`theme`),
    KEY `idx_user_preferences_language` (`language`),
    CONSTRAINT `fk_user_preferences_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`user_id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户偏好设置表';

-- ==================== 用户会话表 ====================

/*
 * 用户会话表 (user_sessions)
 * 存储用户登录会话信息
 */
CREATE TABLE IF NOT EXISTS `user_sessions` (
    `session_id` VARCHAR(128) NOT NULL COMMENT '会话ID (主键)',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键)',
    `device_id` VARCHAR(255) DEFAULT NULL COMMENT '设备ID',
    `device_type` VARCHAR(50) DEFAULT NULL COMMENT '设备类型',
    `client_ip` VARCHAR(45) NOT NULL COMMENT '客户端IP',
    `user_agent` TEXT DEFAULT NULL COMMENT '用户代理',
    `is_active` BOOLEAN NOT NULL DEFAULT TRUE COMMENT '是否活跃',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `expired_at` TIMESTAMP NULL DEFAULT NULL COMMENT '过期时间',
    PRIMARY KEY (`session_id`),
    KEY `idx_user_sessions_user_id` (`user_id`),
    KEY `idx_user_sessions_is_active` (`is_active`),
    KEY `idx_user_sessions_created_at` (`created_at`),
    KEY `idx_user_sessions_expired_at` (`expired_at`),
    CONSTRAINT `fk_user_sessions_user_id` FOREIGN KEY (`user_id`) REFERENCES `users` (`user_id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户会话表';

-- ==================== 用户操作日志表 ====================

/*
 * 用户操作日志表 (user_audit_logs)
 * 记录用户重要操作的审计日志
 */
CREATE TABLE IF NOT EXISTS `user_audit_logs` (
    `log_id` BIGINT AUTO_INCREMENT COMMENT '日志ID (主键)',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID',
    `action` VARCHAR(100) NOT NULL COMMENT '操作类型',
    `details` JSON DEFAULT NULL COMMENT '操作详情JSON',
    `client_ip` VARCHAR(45) NOT NULL COMMENT '客户端IP',
    `user_agent` TEXT DEFAULT NULL COMMENT '用户代理',
    `result` VARCHAR(50) NOT NULL COMMENT '操作结果',
    `error_message` TEXT DEFAULT NULL COMMENT '错误信息',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    PRIMARY KEY (`log_id`),
    KEY `idx_user_audit_logs_user_id` (`user_id`),
    KEY `idx_user_audit_logs_action` (`action`),
    KEY `idx_user_audit_logs_result` (`result`),
    KEY `idx_user_audit_logs_created_at` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci COMMENT='用户操作日志表';

-- ==================== 初始化数据 ====================

-- 插入系统默认用户(可选)
INSERT IGNORE INTO `users` (
    `user_id`, `username`, `email`, `password_hash`, `salt`, `status`
) VALUES (
    'usr_system_admin', 'admin', 'admin@gameservice.com', 
    SHA2(CONCAT('admin123', 'system_salt'), 256), 'system_salt', 0
);

-- 为默认用户创建档案
INSERT IGNORE INTO `user_profiles` (
    `user_id`, `nickname`, `language`, `timezone`
) VALUES (
    'usr_system_admin', '系统管理员', 'zh-CN', 'Asia/Shanghai'
);

-- 为默认用户创建偏好设置
INSERT IGNORE INTO `user_preferences` (
    `user_id`, `theme`, `language`
) VALUES (
    'usr_system_admin', 'light', 'zh-CN'
);

-- ==================== 存储过程 ====================

DELIMITER //

/*
 * 存储过程：创建完整用户
 * 同时创建用户基础信息、档案和偏好设置
 */
CREATE PROCEDURE `CreateCompleteUser`(
    IN p_user_id VARCHAR(64),
    IN p_username VARCHAR(50),
    IN p_email VARCHAR(255),
    IN p_password_hash VARCHAR(255),
    IN p_salt VARCHAR(64),
    IN p_nickname VARCHAR(100)
)
BEGIN
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        RESIGNAL;
    END;

    START TRANSACTION;
    
    -- 创建用户基础信息
    INSERT INTO `users` (
        `user_id`, `username`, `email`, `password_hash`, `salt`
    ) VALUES (
        p_user_id, p_username, p_email, p_password_hash, p_salt
    );
    
    -- 创建用户档案
    INSERT INTO `user_profiles` (
        `user_id`, `nickname`
    ) VALUES (
        p_user_id, COALESCE(p_nickname, p_username)
    );
    
    -- 创建用户偏好设置
    INSERT INTO `user_preferences` (
        `user_id`
    ) VALUES (
        p_user_id
    );
    
    COMMIT;
END //

/*
 * 存储过程：清理过期会话
 */
CREATE PROCEDURE `CleanupExpiredSessions`()
BEGIN
    DELETE FROM `user_sessions` 
    WHERE `expired_at` IS NOT NULL 
    AND `expired_at` < NOW();
    
    SELECT ROW_COUNT() AS cleaned_sessions;
END //

DELIMITER ;

-- ==================== 定期清理任务 ====================

-- 创建事件调度器来定期清理过期会话（需要开启事件调度器）
-- SET GLOBAL event_scheduler = ON;

/*
CREATE EVENT IF NOT EXISTS `evt_cleanup_expired_sessions`
ON SCHEDULE EVERY 1 HOUR
STARTS CURRENT_TIMESTAMP
DO
  CALL CleanupExpiredSessions();
*/

-- ==================== 索引优化建议 ====================

/*
 * 根据实际使用情况，可能需要添加的额外索引：
 * 
 * -- 复合索引：用户状态和在线状态
 * CREATE INDEX `idx_users_status_online` ON `users` (`status`, `online_status`);
 * 
 * -- 复合索引：用户会话查询优化
 * CREATE INDEX `idx_user_sessions_user_active` ON `user_sessions` (`user_id`, `is_active`);
 * 
 * -- 全文索引：用户搜索优化
 * ALTER TABLE `user_profiles` ADD FULLTEXT(`nickname`, `bio`);
 */

-- ==================== 权限设置 ====================

-- 创建用户服务专用数据库用户
-- CREATE USER IF NOT EXISTS 'user_service'@'localhost' IDENTIFIED BY 'user_service_pass123';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON `user_service_db`.* TO 'user_service'@'localhost';
-- FLUSH PRIVILEGES;

-- 显示表结构
SHOW TABLES;




