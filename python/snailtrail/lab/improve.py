from __future__ import annotations

import logging
import time
from pathlib import Path
from typing import Any

import snailtrail

log = logging.getLogger("snailtrail.lab")


def quote(name: str) -> str:
    return "`" + name.replace("`", "``") + "`"


def schema_ddl(cursor: Any, schema: str) -> str:
    cursor.execute(
        "SELECT table_name AS name FROM information_schema.tables "
        "WHERE table_schema = %s AND table_type = 'BASE TABLE' ORDER BY table_name",
        (schema,),
    )
    statements = []
    for row in cursor.fetchall():
        cursor.execute(f"SHOW CREATE TABLE {quote(schema)}.{quote(row['name'])}")
        statements.append(cursor.fetchone()["Create Table"] + ";")
    return "\n\n".join(statements)


def index_statements(report: snailtrail.Report) -> list[str]:
    statements: list[str] = []
    for c in report.classes:
        for finding in c.findings:
            if finding.rule_id != "ST001":
                continue
            first = finding.suggestion.splitlines()[0].strip()
            if first.startswith("ALTER TABLE") and " ADD INDEX " in first and first not in statements:
                statements.append(first)
    return statements


class Improver:
    def __init__(self, connection: Any, slow_log: Path, schema: str) -> None:
        self._connection = connection
        self._slow_log = slow_log
        self._schema = schema

    def plan(self) -> list[str]:
        with self._connection.cursor() as cursor:
            catalog = snailtrail.SchemaCatalog.from_ddl(schema_ddl(cursor, self._schema))
        report = snailtrail.analyze_file(str(self._slow_log), schema=catalog, database=self._schema)
        return index_statements(report)

    def apply(self, statements: list[str]) -> list[str]:
        applied = []
        with self._connection.cursor() as cursor:
            cursor.execute("SET SESSION long_query_time = 3600")
            for statement in statements:
                try:
                    cursor.execute(statement)
                    applied.append(statement)
                    log.info("applied: %s", statement)
                except Exception as error:
                    log.warning("skipped %s: %s", statement, error)
        self._connection.commit()
        return applied

    def rotate(self) -> Path | None:
        if not self._slow_log.exists():
            return None
        archived = self._slow_log.with_name(f"{self._slow_log.stem}-{time.strftime('%Y%m%d-%H%M%S')}.log")
        self._slow_log.rename(archived)
        with self._connection.cursor() as cursor:
            cursor.execute("FLUSH SLOW LOGS")
        log.info("rotated the slow log to %s", archived)
        return archived
