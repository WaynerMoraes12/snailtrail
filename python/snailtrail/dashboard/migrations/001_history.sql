CREATE TABLE IF NOT EXISTS analysis_runs (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    created_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
    source VARCHAR(512) NOT NULL,
    log_bytes BIGINT UNSIGNED NOT NULL,
    events BIGINT UNSIGNED NOT NULL,
    skipped BIGINT UNSIGNED NOT NULL,
    class_count INT UNSIGNED NOT NULL,
    total_query_time_us BIGINT UNSIGNED NOT NULL,
    total_rows_examined BIGINT UNSIGNED NOT NULL,
    window_start DATETIME NULL,
    window_end DATETIME NULL,
    threads SMALLINT UNSIGNED NOT NULL,
    parse_seconds DOUBLE NOT NULL,
    schema_tables SMALLINT UNSIGNED NOT NULL,
    critical INT UNSIGNED NOT NULL,
    warning INT UNSIGNED NOT NULL,
    info INT UNSIGNED NOT NULL,
    PRIMARY KEY (id),
    KEY idx_runs_created_at (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS query_classes (
    digest CHAR(16) NOT NULL,
    kind VARCHAR(16) NOT NULL,
    fingerprint MEDIUMTEXT NOT NULL,
    label VARCHAR(255) NOT NULL,
    first_run_id BIGINT UNSIGNED NOT NULL,
    last_run_id BIGINT UNSIGNED NOT NULL,
    PRIMARY KEY (digest),
    KEY idx_classes_last_run (last_run_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS class_snapshots (
    run_id BIGINT UNSIGNED NOT NULL,
    digest CHAR(16) NOT NULL,
    rank_no INT UNSIGNED NOT NULL,
    calls BIGINT UNSIGNED NOT NULL,
    time_share DOUBLE NOT NULL,
    total_time_us BIGINT UNSIGNED NOT NULL,
    avg_time_us DOUBLE NOT NULL,
    p50_us BIGINT UNSIGNED NOT NULL,
    p95_us BIGINT UNSIGNED NOT NULL,
    p99_us BIGINT UNSIGNED NOT NULL,
    max_us BIGINT UNSIGNED NOT NULL,
    lock_time_us BIGINT UNSIGNED NOT NULL,
    rows_examined BIGINT UNSIGNED NOT NULL,
    rows_sent BIGINT UNSIGNED NOT NULL,
    full_scan_ratio DOUBLE NOT NULL,
    filesort_ratio DOUBLE NOT NULL,
    tmp_disk_ratio DOUBLE NOT NULL,
    latency_decades JSON NOT NULL,
    sample_sql MEDIUMTEXT NOT NULL,
    sample_database VARCHAR(64) NOT NULL,
    sample_time_us BIGINT UNSIGNED NOT NULL,
    explain_plan JSON NULL,
    PRIMARY KEY (run_id, digest),
    KEY idx_snapshots_digest_run (digest, run_id),
    CONSTRAINT fk_snapshots_run FOREIGN KEY (run_id) REFERENCES analysis_runs (id) ON DELETE CASCADE,
    CONSTRAINT fk_snapshots_class FOREIGN KEY (digest) REFERENCES query_classes (digest)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;

CREATE TABLE IF NOT EXISTS findings (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    run_id BIGINT UNSIGNED NOT NULL,
    digest CHAR(16) NOT NULL,
    rule_id CHAR(5) NOT NULL,
    rule_name VARCHAR(64) NOT NULL,
    severity ENUM('info', 'warning', 'critical') NOT NULL,
    title VARCHAR(512) NOT NULL,
    detail TEXT NOT NULL,
    suggestion TEXT NOT NULL,
    PRIMARY KEY (id),
    KEY idx_findings_run_severity (run_id, severity),
    KEY idx_findings_rule (rule_id, run_id),
    CONSTRAINT fk_findings_snapshot FOREIGN KEY (run_id, digest)
        REFERENCES class_snapshots (run_id, digest) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
