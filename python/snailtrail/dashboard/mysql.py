from __future__ import annotations

import json
import logging
from collections.abc import Iterator, Mapping, Sequence
from contextlib import contextmanager
from importlib import resources
from typing import Any

import pymysql
import pymysql.cursors

import snailtrail

from .config import MySQLDsn
from .explain import parse_plan
from .model import (
    Change,
    ClassSnapshot,
    FindingRow,
    HistoryPoint,
    Plan,
    RunSummary,
    run_fields,
    snapshots_from_report,
)

log = logging.getLogger("snailtrail.dashboard")

_UNSET = object()


def quote_identifier(name: str) -> str:
    return "`" + name.replace("`", "``") + "`"


class Database:
    def __init__(self, dsn: MySQLDsn, connect_timeout: int = 5) -> None:
        self.dsn = dsn
        self._timeout = connect_timeout

    def connect(self, database: Any = _UNSET) -> pymysql.connections.Connection:
        return pymysql.connect(
            host=self.dsn.host,
            port=self.dsn.port,
            user=self.dsn.user,
            password=self.dsn.password,
            database=self.dsn.database if database is _UNSET else database,
            charset="utf8mb4",
            cursorclass=pymysql.cursors.DictCursor,
            connect_timeout=self._timeout,
            autocommit=False,
        )

    @contextmanager
    def cursor(self, database: Any = _UNSET) -> Iterator[pymysql.cursors.DictCursor]:
        connection = self.connect(database)
        try:
            with connection.cursor() as cursor:
                yield cursor
            connection.commit()
        except BaseException:
            connection.rollback()
            raise
        finally:
            connection.close()

    def create_database_if_missing(self) -> None:
        with self.cursor(None) as cursor:
            cursor.execute(
                f"CREATE DATABASE IF NOT EXISTS {quote_identifier(self.dsn.database)} "
                "CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci"
            )

    def ping(self) -> bool:
        try:
            with self.cursor() as cursor:
                cursor.execute("SELECT 1")
            return True
        except pymysql.MySQLError:
            return False


class Migrator:
    def __init__(self, database: Database, package: str = "snailtrail.dashboard") -> None:
        self._database = database
        self._package = package

    def migrations(self) -> list[tuple[str, str]]:
        folder = resources.files(self._package) / "migrations"
        files = sorted((f for f in folder.iterdir() if f.name.endswith(".sql")), key=lambda f: f.name)
        return [(f.name.removesuffix(".sql"), f.read_text(encoding="utf-8")) for f in files]

    def applied(self) -> set[str]:
        with self._database.cursor() as cursor:
            cursor.execute(
                "CREATE TABLE IF NOT EXISTS schema_migrations ("
                "version VARCHAR(128) NOT NULL PRIMARY KEY, "
                "applied_at DATETIME(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6))"
            )
            cursor.execute("SELECT version FROM schema_migrations")
            return {row["version"] for row in cursor.fetchall()}

    def apply(self) -> list[str]:
        done = self.applied()
        applied: list[str] = []
        for version, sql in self.migrations():
            if version in done:
                continue
            with self._database.cursor() as cursor:
                for statement in (s.strip() for s in sql.split(";")):
                    if statement:
                        cursor.execute(statement)
                cursor.execute("INSERT INTO schema_migrations (version) VALUES (%s)", (version,))
            log.info("applied migration %s", version)
            applied.append(version)
        return applied


_RUN_COLUMNS = (
    "source, log_bytes, events, skipped, class_count, total_query_time_us, total_rows_examined, "
    "window_start, window_end, threads, parse_seconds, schema_tables, critical, warning, info"
)

_SNAPSHOT_COLUMNS = (
    "run_id, digest, rank_no, calls, time_share, total_time_us, avg_time_us, p50_us, p95_us, p99_us, "
    "max_us, lock_time_us, rows_examined, rows_sent, full_scan_ratio, filesort_ratio, tmp_disk_ratio, "
    "latency_decades, sample_sql, sample_database, sample_time_us, explain_plan"
)

_SEVERITY_ORDER = "FIELD(severity, 'critical', 'warning', 'info')"

_CHANGES_SQL = """
WITH ordered AS (
    SELECT s.run_id, s.digest, s.avg_time_us,
           LAG(s.avg_time_us) OVER (PARTITION BY s.digest ORDER BY s.run_id) AS previous_avg_us
    FROM class_snapshots s
    WHERE s.run_id <= %s
      AND s.digest IN (SELECT digest FROM class_snapshots WHERE run_id = %s)
)
SELECT o.digest, c.label, o.previous_avg_us, o.avg_time_us,
       o.avg_time_us / o.previous_avg_us AS ratio
FROM ordered o
JOIN query_classes c ON c.digest = o.digest
WHERE o.run_id = %s
  AND o.previous_avg_us > 0
  AND o.avg_time_us > 0
  AND (o.avg_time_us / o.previous_avg_us >= %s OR o.avg_time_us / o.previous_avg_us <= 1 / %s)
ORDER BY ratio DESC
"""


def _run(row: Mapping[str, Any]) -> RunSummary:
    return RunSummary(
        id=row["id"],
        created_at=row["created_at"],
        source=row["source"],
        log_bytes=row["log_bytes"],
        events=row["events"],
        skipped=row["skipped"],
        class_count=row["class_count"],
        total_query_time_us=row["total_query_time_us"],
        total_rows_examined=row["total_rows_examined"],
        window_start=row["window_start"],
        window_end=row["window_end"],
        threads=row["threads"],
        parse_seconds=row["parse_seconds"],
        schema_tables=row["schema_tables"],
        critical=row["critical"],
        warning=row["warning"],
        info=row["info"],
    )


def _finding(row: Mapping[str, Any]) -> FindingRow:
    return FindingRow(row["rule_id"], row["rule_name"], row["severity"], row["title"], row["detail"], row["suggestion"])


def _snapshot(row: Mapping[str, Any], findings: Sequence[FindingRow]) -> ClassSnapshot:
    plan = row["explain_plan"]
    return ClassSnapshot(
        run_id=row["run_id"],
        digest=row["digest"],
        rank=row["rank_no"],
        kind=row["kind"],
        label=row["label"],
        fingerprint=row["fingerprint"],
        calls=row["calls"],
        time_share=row["time_share"],
        total_time_us=row["total_time_us"],
        avg_time_us=row["avg_time_us"],
        p50_us=row["p50_us"],
        p95_us=row["p95_us"],
        p99_us=row["p99_us"],
        max_us=row["max_us"],
        lock_time_us=row["lock_time_us"],
        rows_examined=row["rows_examined"],
        rows_sent=row["rows_sent"],
        full_scan_ratio=row["full_scan_ratio"],
        filesort_ratio=row["filesort_ratio"],
        tmp_disk_ratio=row["tmp_disk_ratio"],
        latency_decades=tuple(json.loads(row["latency_decades"])),
        sample_sql=row["sample_sql"],
        sample_database=row["sample_database"],
        sample_time_us=row["sample_time_us"],
        plan=parse_plan(json.loads(plan)) if plan else None,
        findings=tuple(findings),
    )


class MySQLHistory:
    def __init__(self, database: Database) -> None:
        self._database = database

    def save(self, report: snailtrail.Report, plans: Mapping[str, Plan] | None = None) -> int:
        fields = run_fields(report)
        with self._database.cursor() as cursor:
            cursor.execute(
                f"INSERT INTO analysis_runs ({_RUN_COLUMNS}) VALUES ({', '.join(['%s'] * 15)})",
                tuple(fields[c.strip()] for c in _RUN_COLUMNS.split(",")),
            )
            run_id = cursor.lastrowid
            snapshots = snapshots_from_report(report, run_id, plans)
            for s in snapshots:
                cursor.execute(
                    "INSERT INTO query_classes (digest, kind, fingerprint, label, first_run_id, last_run_id) "
                    "VALUES (%s, %s, %s, %s, %s, %s) AS new "
                    "ON DUPLICATE KEY UPDATE label = new.label, last_run_id = new.last_run_id",
                    (s.digest, s.kind, s.fingerprint, s.label, run_id, run_id),
                )
            cursor.executemany(
                f"INSERT INTO class_snapshots ({_SNAPSHOT_COLUMNS}) VALUES ({', '.join(['%s'] * 22)})",
                [
                    (
                        run_id,
                        s.digest,
                        s.rank,
                        s.calls,
                        s.time_share,
                        s.total_time_us,
                        s.avg_time_us,
                        s.p50_us,
                        s.p95_us,
                        s.p99_us,
                        s.max_us,
                        s.lock_time_us,
                        s.rows_examined,
                        s.rows_sent,
                        s.full_scan_ratio,
                        s.filesort_ratio,
                        s.tmp_disk_ratio,
                        json.dumps(list(s.latency_decades)),
                        s.sample_sql,
                        s.sample_database,
                        s.sample_time_us,
                        json.dumps(dict(s.plan.raw)) if s.plan else None,
                    )
                    for s in snapshots
                ],
            )
            cursor.executemany(
                "INSERT INTO findings (run_id, digest, rule_id, rule_name, severity, title, detail, suggestion) "
                "VALUES (%s, %s, %s, %s, %s, %s, %s, %s)",
                [
                    (run_id, s.digest, f.rule_id, f.rule_name, f.severity, f.title, f.detail, f.suggestion)
                    for s in snapshots
                    for f in s.findings
                ],
            )
        return run_id

    def runs(self, limit: int = 50) -> list[RunSummary]:
        with self._database.cursor() as cursor:
            cursor.execute("SELECT * FROM analysis_runs ORDER BY id DESC LIMIT %s", (limit,))
            return [_run(r) for r in cursor.fetchall()]

    def run(self, run_id: int) -> RunSummary | None:
        with self._database.cursor() as cursor:
            cursor.execute("SELECT * FROM analysis_runs WHERE id = %s", (run_id,))
            row = cursor.fetchone()
            return _run(row) if row else None

    def latest_run(self) -> RunSummary | None:
        runs = self.runs(1)
        return runs[0] if runs else None

    def _findings_by_digest(self, cursor: Any, run_id: int, digest: str | None = None) -> dict[str, list[FindingRow]]:
        sql = "SELECT * FROM findings WHERE run_id = %s"
        args: tuple[Any, ...] = (run_id,)
        if digest is not None:
            sql += " AND digest = %s"
            args += (digest,)
        cursor.execute(sql + f" ORDER BY {_SEVERITY_ORDER}, rule_id, id", args)
        grouped: dict[str, list[FindingRow]] = {}
        for row in cursor.fetchall():
            grouped.setdefault(row["digest"], []).append(_finding(row))
        return grouped

    def classes(self, run_id: int) -> list[ClassSnapshot]:
        with self._database.cursor() as cursor:
            cursor.execute(
                "SELECT s.*, c.kind, c.label, c.fingerprint FROM class_snapshots s "
                "JOIN query_classes c ON c.digest = s.digest WHERE s.run_id = %s ORDER BY s.rank_no",
                (run_id,),
            )
            rows = cursor.fetchall()
            findings = self._findings_by_digest(cursor, run_id)
        return [_snapshot(r, findings.get(r["digest"], [])) for r in rows]

    def snapshot(self, run_id: int, digest: str) -> ClassSnapshot | None:
        with self._database.cursor() as cursor:
            cursor.execute(
                "SELECT s.*, c.kind, c.label, c.fingerprint FROM class_snapshots s "
                "JOIN query_classes c ON c.digest = s.digest WHERE s.run_id = %s AND s.digest = %s",
                (run_id, digest),
            )
            row = cursor.fetchone()
            if row is None:
                return None
            findings = self._findings_by_digest(cursor, run_id, digest)
        return _snapshot(row, findings.get(digest, []))

    def history(self, digest: str, limit: int = 30) -> list[HistoryPoint]:
        with self._database.cursor() as cursor:
            cursor.execute(
                "SELECT r.id, r.created_at, s.calls, s.total_time_us, s.avg_time_us, s.p95_us "
                "FROM class_snapshots s JOIN analysis_runs r ON r.id = s.run_id "
                "WHERE s.digest = %s ORDER BY s.run_id DESC LIMIT %s",
                (digest, limit),
            )
            rows = cursor.fetchall()
        return [
            HistoryPoint(r["id"], r["created_at"], r["calls"], r["total_time_us"], r["avg_time_us"], r["p95_us"])
            for r in reversed(rows)
        ]

    def changes(self, run_id: int, threshold: float) -> tuple[list[Change], list[Change]]:
        with self._database.cursor() as cursor:
            cursor.execute(
                _CHANGES_SQL,
                (run_id, run_id, run_id, threshold, threshold),
            )
            rows = cursor.fetchall()
        changes = [Change(r["digest"], r["label"], r["previous_avg_us"], r["avg_time_us"]) for r in rows]
        regressions = [c for c in changes if c.ratio >= threshold]
        improvements = sorted((c for c in changes if c.ratio < threshold), key=lambda c: c.ratio)
        return regressions, improvements

    def findings(self, run_id: int, rule_id: str | None = None) -> list[tuple[str, FindingRow]]:
        with self._database.cursor() as cursor:
            grouped = self._findings_by_digest(cursor, run_id)
        return [
            (digest, f) for digest, rows in grouped.items() for f in rows if rule_id is None or f.rule_id == rule_id
        ]


class MySQLSchemaSource:
    def __init__(self, database: Database, schemas: Sequence[str]) -> None:
        self._database = database
        self._schemas = list(schemas)

    def ddl(self) -> str:
        statements: list[str] = []
        with self._database.cursor(None) as cursor:
            for schema in self._schemas:
                cursor.execute(
                    "SELECT table_name AS name FROM information_schema.tables "
                    "WHERE table_schema = %s AND table_type = 'BASE TABLE' ORDER BY table_name",
                    (schema,),
                )
                for row in cursor.fetchall():
                    cursor.execute(f"SHOW CREATE TABLE {quote_identifier(schema)}.{quote_identifier(row['name'])}")
                    statements.append(cursor.fetchone()["Create Table"])
        return "".join(s + ";\n\n" for s in statements)
