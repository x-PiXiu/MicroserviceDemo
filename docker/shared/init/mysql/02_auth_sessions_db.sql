-- =============================================================================
-- 认证服务专用数据库设计 (修复版)
-- 版本: 2.0.1
-- 创建日期: 2025-09-19
-- 更新日期: 2025-02-17
-- 描述: 认证服务专用数据库，只管理会话和认证相关数据
-- 注意: 用户基础数据已迁移到 user_service_db
--
-- 修复说明 (v2.0.1):
-- 1. 移除对 information_schema 的直接查询，避免权限问题
-- 2. 注释掉需要 SUPER 权限的 SET GLOBAL 语句
-- 3. 注释掉需要 event_scheduler 权限的 CREATE EVENT 语句
-- 4. 建议由应用程序定时调用存储过程进行清理
--
-- 执行方式:
-- mysql -u root -p < auth_sessions_database_design_fixed.sql
-- 或在 MySQL 客户端中: source /path/to/auth_sessions_database_design_fixed.sql
-- =============================================================================

-- 创建认证服务专用数据库
CREATE DATABASE IF NOT EXISTS `auth_sessions_db` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE `auth_sessions_db`;

-- =============================================================================
-- 1. 会话管理表
-- =============================================================================

-- 用户会话表 (从 game_microservices 迁移并优化)
CREATE TABLE `user_sessions` (
    `session_id` VARCHAR(128) PRIMARY KEY COMMENT '会话唯一标识',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID (外键指向 user_service_db.users)',
    `device_type` VARCHAR(50) DEFAULT '' COMMENT '设备类型 (mobile/desktop/web)',
    `device_id` VARCHAR(200) DEFAULT '' COMMENT '设备唯一标识',
    `client_ip` VARCHAR(45) DEFAULT '' COMMENT '客户端IP地址',
    `user_agent` TEXT COMMENT '用户代理字符串',
    `session_data` JSON NULL COMMENT '会话附加数据',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `last_activity_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '最后活动时间',
    `expires_at` TIMESTAMP NOT NULL COMMENT '过期时间',
    `is_active` BOOLEAN DEFAULT TRUE COMMENT '是否活跃',
    `login_method` VARCHAR(50) DEFAULT 'password' COMMENT '登录方式 (password/oauth/sso)',
    `refresh_token_hash` VARCHAR(255) DEFAULT '' COMMENT 'Refresh Token哈希值',
    
    INDEX idx_user_id (user_id),
    INDEX idx_device_id (device_id),
    INDEX idx_expires_at (expires_at),
    INDEX idx_is_active (is_active),
    INDEX idx_last_activity_at (last_activity_at),
    INDEX idx_created_at (created_at),
    INDEX idx_client_ip (client_ip)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户会话表';

-- =============================================================================
-- 2. JWT令牌管理
-- =============================================================================

-- JWT令牌黑名单表 (用于登出和撤销)
CREATE TABLE `jwt_blacklist` (
    `token_id` VARCHAR(255) PRIMARY KEY COMMENT '令牌ID (jti声明)',
    `user_id` VARCHAR(64) NOT NULL COMMENT '用户ID',
    `token_type` ENUM('access', 'refresh') NOT NULL COMMENT '令牌类型',
    `token_hash` VARCHAR(255) NOT NULL COMMENT '令牌哈希值 (部分)',
    `expires_at` TIMESTAMP NOT NULL COMMENT '令牌原始过期时间',
    `blacklisted_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '加入黑名单时间',
    `reason` VARCHAR(255) DEFAULT '' COMMENT '加入黑名单原因',
    `operator_id` VARCHAR(64) DEFAULT '' COMMENT '操作者ID (管理员撤销时)',
    
    INDEX idx_user_id (user_id),
    INDEX idx_token_type (token_type),
    INDEX idx_expires_at (expires_at),
    INDEX idx_blacklisted_at (blacklisted_at),
    INDEX idx_token_hash (token_hash)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='JWT令牌黑名单';

-- =============================================================================
-- 3. 认证审计日志
-- =============================================================================

-- 认证审计日志表 (增强版)
CREATE TABLE `auth_audit_logs` (
    `log_id` BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '日志ID',
    `user_id` VARCHAR(64) NULL COMMENT '用户ID (可为空，如登录失败)',
    `username` VARCHAR(50) DEFAULT '' COMMENT '尝试登录的用户名',
    `action` VARCHAR(50) NOT NULL COMMENT '操作类型 (login/logout/register/validate/refresh)',
    `success` BOOLEAN NOT NULL COMMENT '操作是否成功',
    `client_ip` VARCHAR(45) DEFAULT '' COMMENT '客户端IP地址',
    `user_agent` TEXT COMMENT '用户代理字符串',
    `session_id` VARCHAR(128) DEFAULT '' COMMENT '关联的会话ID',
    `device_id` VARCHAR(200) DEFAULT '' COMMENT '设备ID',
    `error_code` VARCHAR(50) DEFAULT '' COMMENT '错误代码',
    `error_message` TEXT COMMENT '错误详细信息',
    `additional_data` JSON NULL COMMENT '附加数据 (请求参数等)',
    `processing_time_ms` INT DEFAULT 0 COMMENT '处理时间 (毫秒)',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    
    INDEX idx_user_id (user_id),
    INDEX idx_username (username),
    INDEX idx_action (action),
    INDEX idx_success (success),
    INDEX idx_client_ip (client_ip),
    INDEX idx_session_id (session_id),
    INDEX idx_created_at (created_at),
    INDEX idx_error_code (error_code)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='认证审计日志表';

-- =============================================================================
-- 4. 安全配置和限流
-- =============================================================================

-- 登录限流表 (防暴力破解)
CREATE TABLE `login_rate_limits` (
    `limit_id` BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '限流记录ID',
    `identifier` VARCHAR(255) NOT NULL COMMENT '限流标识 (IP/用户名/设备ID)',
    `identifier_type` ENUM('ip', 'username', 'device', 'user_id') NOT NULL COMMENT '标识类型',
    `attempt_count` INT DEFAULT 1 COMMENT '尝试次数',
    `first_attempt_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '首次尝试时间',
    `last_attempt_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '最后尝试时间',
    `blocked_until` TIMESTAMP NULL COMMENT '阻止到期时间',
    `is_blocked` BOOLEAN DEFAULT FALSE COMMENT '是否被阻止',
    `window_start` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '时间窗口开始',
    
    UNIQUE KEY uk_identifier_type (identifier, identifier_type),
    INDEX idx_identifier (identifier),
    INDEX idx_blocked_until (blocked_until),
    INDEX idx_is_blocked (is_blocked),
    INDEX idx_last_attempt_at (last_attempt_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='登录限流表';

-- =============================================================================
-- 5. 安全事件记录
-- =============================================================================

-- 安全事件表 (可疑活动监控)
CREATE TABLE `security_events` (
    `event_id` BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '事件ID',
    `user_id` VARCHAR(64) NULL COMMENT '用户ID',
    `event_type` VARCHAR(100) NOT NULL COMMENT '事件类型',
    `severity` ENUM('low', 'medium', 'high', 'critical') DEFAULT 'medium' COMMENT '严重程度',
    `description` TEXT NOT NULL COMMENT '事件描述',
    `client_ip` VARCHAR(45) DEFAULT '' COMMENT '客户端IP',
    `user_agent` TEXT COMMENT '用户代理',
    `session_id` VARCHAR(128) DEFAULT '' COMMENT '关联会话ID',
    `geo_location` JSON NULL COMMENT '地理位置信息',
    `risk_score` DECIMAL(3,2) DEFAULT 0.00 COMMENT '风险评分 (0.00-1.00)',
    `is_resolved` BOOLEAN DEFAULT FALSE COMMENT '是否已处理',
    `resolved_at` TIMESTAMP NULL COMMENT '处理时间',
    `resolver_id` VARCHAR(64) DEFAULT '' COMMENT '处理人ID',
    `resolution_notes` TEXT COMMENT '处理说明',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    
    INDEX idx_user_id (user_id),
    INDEX idx_event_type (event_type),
    INDEX idx_severity (severity),
    INDEX idx_client_ip (client_ip),
    INDEX idx_session_id (session_id),
    INDEX idx_risk_score (risk_score),
    INDEX idx_is_resolved (is_resolved),
    INDEX idx_created_at (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='安全事件表';

-- =============================================================================
-- 6. 系统配置表
-- =============================================================================

-- 认证服务配置表 (动态配置)
CREATE TABLE `auth_config` (
    `config_id` BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '配置ID',
    `config_key` VARCHAR(100) NOT NULL UNIQUE COMMENT '配置键',
    `config_value` TEXT NOT NULL COMMENT '配置值',
    `config_type` ENUM('string', 'number', 'boolean', 'json') DEFAULT 'string' COMMENT '配置类型',
    `description` TEXT COMMENT '配置描述',
    `is_encrypted` BOOLEAN DEFAULT FALSE COMMENT '是否加密存储',
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    `updated_by` VARCHAR(64) DEFAULT '' COMMENT '更新者',
    
    INDEX idx_config_key (config_key),
    INDEX idx_config_type (config_type),
    INDEX idx_updated_at (updated_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='认证服务配置表';

-- =============================================================================
-- 7. 默认配置数据
-- =============================================================================

-- 插入默认认证配置
INSERT INTO `auth_config` (`config_key`, `config_value`, `config_type`, `description`) VALUES
('max_login_attempts', '5', 'number', '最大登录尝试次数'),
('login_lockout_duration_minutes', '15', 'number', '登录锁定时长(分钟)'),
('session_timeout_hours', '24', 'number', '会话超时时长(小时)'),
('refresh_token_expiry_days', '7', 'number', 'Refresh Token过期天数'),
('require_device_verification', 'false', 'boolean', '是否需要设备验证'),
('enable_geo_blocking', 'false', 'boolean', '是否启用地理位置阻止'),
('max_sessions_per_user', '5', 'number', '每用户最大并发会话数'),
('jwt_secret_rotation_days', '30', 'number', 'JWT密钥轮换天数'),
('enable_security_monitoring', 'true', 'boolean', '是否启用安全监控'),
('suspicious_activity_threshold', '0.7', 'number', '可疑活动阈值');

-- =============================================================================
-- 8. 存储过程
-- =============================================================================

DELIMITER $$

-- 清理过期会话
CREATE PROCEDURE `sp_cleanup_expired_sessions`()
BEGIN
    DECLARE cleaned_count INT DEFAULT 0;
    
    -- 删除过期的会话
    DELETE FROM `user_sessions` 
    WHERE `expires_at` < NOW() OR (`is_active` = FALSE AND `last_activity_at` < DATE_SUB(NOW(), INTERVAL 7 DAY));
    
    SET cleaned_count = ROW_COUNT();
    
    -- 记录清理日志
    INSERT INTO `auth_audit_logs` (`action`, `success`, `additional_data`, `created_at`)
    VALUES ('session_cleanup', TRUE, JSON_OBJECT('cleaned_sessions', cleaned_count), NOW());
    
    SELECT cleaned_count as cleaned_sessions;
END$$

-- 清理过期的JWT黑名单
CREATE PROCEDURE `sp_cleanup_expired_jwt_blacklist`()
BEGIN
    DECLARE cleaned_count INT DEFAULT 0;
    
    -- 删除过期的黑名单令牌
    DELETE FROM `jwt_blacklist` 
    WHERE `expires_at` < NOW();
    
    SET cleaned_count = ROW_COUNT();
    
    -- 记录清理日志
    INSERT INTO `auth_audit_logs` (`action`, `success`, `additional_data`, `created_at`)
    VALUES ('jwt_blacklist_cleanup', TRUE, JSON_OBJECT('cleaned_tokens', cleaned_count), NOW());
    
    SELECT cleaned_count as cleaned_tokens;
END$$

-- 检查登录限流
CREATE PROCEDURE `sp_check_login_rate_limit`(
    IN p_identifier VARCHAR(255),
    IN p_identifier_type ENUM('ip', 'username', 'device', 'user_id'),
    IN p_max_attempts INT,
    IN p_window_minutes INT,
    OUT p_is_blocked BOOLEAN,
    OUT p_remaining_attempts INT
)
BEGIN
    DECLARE current_attempts INT DEFAULT 0;
    DECLARE window_start_time TIMESTAMP;
    DECLARE blocked_until_time TIMESTAMP;
    
    SET window_start_time = DATE_SUB(NOW(), INTERVAL p_window_minutes MINUTE);
    
    -- 获取当前限流记录
    SELECT 
        attempt_count, 
        blocked_until
    INTO 
        current_attempts, 
        blocked_until_time
    FROM `login_rate_limits`
    WHERE identifier = p_identifier 
      AND identifier_type = p_identifier_type
      AND window_start >= window_start_time;
    
    -- 检查是否仍在阻止期内
    IF blocked_until_time IS NOT NULL AND blocked_until_time > NOW() THEN
        SET p_is_blocked = TRUE;
        SET p_remaining_attempts = 0;
    ELSE
        SET p_is_blocked = FALSE;
        SET p_remaining_attempts = GREATEST(0, p_max_attempts - COALESCE(current_attempts, 0));
    END IF;
END$$

-- 更新用户会话活动时间
CREATE PROCEDURE `sp_update_session_activity`(
    IN p_session_id VARCHAR(128)
)
BEGIN
    UPDATE `user_sessions` 
    SET `last_activity_at` = NOW()
    WHERE `session_id` = p_session_id AND `is_active` = TRUE;
    
    IF ROW_COUNT() > 0 THEN
        SELECT TRUE as updated;
    ELSE
        SELECT FALSE as updated;
    END IF;
END$$

DELIMITER ;

-- =============================================================================
-- 9. 创建视图
-- =============================================================================

-- 活跃会话统计视图
CREATE VIEW `v_active_sessions_stats` AS
SELECT 
    COUNT(*) as total_active_sessions,
    COUNT(DISTINCT user_id) as unique_users,
    COUNT(DISTINCT client_ip) as unique_ips,
    COUNT(DISTINCT device_type) as device_types,
    AVG(TIMESTAMPDIFF(MINUTE, created_at, last_activity_at)) as avg_session_duration_minutes,
    DATE(created_at) as session_date
FROM `user_sessions`
WHERE `is_active` = TRUE 
  AND `expires_at` > NOW()
GROUP BY DATE(created_at)
ORDER BY session_date DESC;

-- 安全事件统计视图
CREATE VIEW `v_security_events_stats` AS
SELECT 
    event_type,
    severity,
    COUNT(*) as event_count,
    COUNT(DISTINCT user_id) as affected_users,
    COUNT(DISTINCT client_ip) as source_ips,
    AVG(risk_score) as avg_risk_score,
    DATE(created_at) as event_date
FROM `security_events`
WHERE created_at >= DATE_SUB(NOW(), INTERVAL 30 DAY)
GROUP BY event_type, severity, DATE(created_at)
ORDER BY event_date DESC, event_count DESC;

-- 登录失败统计视图
CREATE VIEW `v_login_failure_stats` AS
SELECT 
    DATE(created_at) as failure_date,
    COUNT(*) as total_failures,
    COUNT(DISTINCT username) as failed_usernames,
    COUNT(DISTINCT client_ip) as source_ips,
    error_code,
    COUNT(CASE WHEN error_code = 'INVALID_CREDENTIALS' THEN 1 END) as credential_failures,
    COUNT(CASE WHEN error_code = 'ACCOUNT_LOCKED' THEN 1 END) as locked_accounts,
    COUNT(CASE WHEN error_code = 'RATE_LIMITED' THEN 1 END) as rate_limited
FROM `auth_audit_logs`
WHERE `action` = 'login' 
  AND `success` = FALSE
  AND created_at >= DATE_SUB(NOW(), INTERVAL 30 DAY)
GROUP BY DATE(created_at), error_code
ORDER BY failure_date DESC;

-- =============================================================================
-- 10. 触发器 (自动化维护)
-- =============================================================================

-- 自动更新会话活动时间的触发器
DELIMITER $$
CREATE TRIGGER `tr_update_session_activity` 
BEFORE UPDATE ON `user_sessions`
FOR EACH ROW
BEGIN
    -- 如果会话数据发生变化，更新活动时间
    IF OLD.session_data IS NOT NULL AND NEW.session_data IS NOT NULL 
       AND OLD.session_data != NEW.session_data THEN
        SET NEW.last_activity_at = NOW();
    END IF;
END$$
DELIMITER ;

-- =============================================================================
-- 11. 定期清理任务 (事件调度器)
-- =============================================================================

-- 注意: 以下语句需要 SUPER 权限，如果权限不足请跳过或在 my.cnf 中配置
-- 启用事件调度器 (如果已有权限则取消注释)
-- SET GLOBAL event_scheduler = ON;

-- 每小时清理过期会话 (需要 event_scheduler = ON)
-- CREATE EVENT IF NOT EXISTS `evt_cleanup_expired_sessions`
-- ON SCHEDULE EVERY 1 HOUR
-- DO
--   CALL sp_cleanup_expired_sessions();

-- 每天清理过期JWT黑名单 (需要 event_scheduler = ON)
-- CREATE EVENT IF NOT EXISTS `evt_cleanup_jwt_blacklist`
-- ON SCHEDULE EVERY 1 DAY
-- STARTS '2025-09-19 02:00:00'
-- DO
--   CALL sp_cleanup_expired_jwt_blacklist();

-- 每周清理旧的审计日志 (保留90天) (需要 event_scheduler = ON)
-- CREATE EVENT IF NOT EXISTS `evt_cleanup_old_audit_logs`
-- ON SCHEDULE EVERY 1 WEEK
-- STARTS '2025-09-19 03:00:00'
-- DO
--   DELETE FROM `auth_audit_logs` WHERE `created_at` < DATE_SUB(NOW(), INTERVAL 90 DAY);

-- 替代方案：由应用程序定时调用存储过程
-- 参考调用: CALL sp_cleanup_expired_sessions();
-- 参考调用: CALL sp_cleanup_expired_jwt_blacklist();

-- =============================================================================
-- 12. 性能优化
-- =============================================================================

-- 分析表以优化性能
ANALYZE TABLE `user_sessions`, `jwt_blacklist`, `auth_audit_logs`, `login_rate_limits`, `security_events`, `auth_config`;

-- =============================================================================
-- 13. 完成信息
-- =============================================================================

-- 显示完成信息
SELECT
    'Authentication Sessions Database Created Successfully' as status,
    NOW() as completion_time,
    'Database optimized for auth service operations' as description,
    'Tables: sessions, jwt_blacklist, audit_logs, rate_limits, security_events, config' as tables_created;

-- 显示表统计 (使用 SHOW TABLES 替代 information_schema 查询，避免权限问题)
SHOW TABLE STATUS FROM `auth_sessions_db`;

-- 显示当前数据库的表列表
SHOW TABLES FROM `auth_sessions_db`;

