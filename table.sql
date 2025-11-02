use image_classify_auth

-- CDK 主表
CREATE TABLE IF NOT EXISTS cdk (
  code VARCHAR(64) PRIMARY KEY,
  status ENUM('active','revoked') NOT NULL DEFAULT 'active',
  allowed_devices INT NOT NULL DEFAULT 2,
  expires_at DATETIME NULL,
  meta JSON NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 设备绑定记录（一个 CDK 最多 allowed_devices 行）
CREATE TABLE IF NOT EXISTS activation (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  cdk_code VARCHAR(64) NOT NULL,
  device_id VARCHAR(128) NOT NULL,
  app_version VARCHAR(32) NULL,
  activated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY uniq_cdk_device (cdk_code, device_id),
  KEY idx_cdk (cdk_code),
  CONSTRAINT fk_activation_cdk FOREIGN KEY (cdk_code) REFERENCES cdk(code) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 示例数据
INSERT IGNORE INTO cdk(code, allowed_devices, status, expires_at)
VALUES ('TEST-NEW-2222-3333', 2, 'active', DATE_ADD(UTC_TIMESTAMP(), INTERVAL 365 DAY));
