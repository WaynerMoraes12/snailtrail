from __future__ import annotations

import json
import logging
from collections.abc import Iterator, Mapping
from typing import Any

import snailtrail

from .model import Plan, PlanRow

log = logging.getLogger("snailtrail.dashboard")

_FILESORT_KEYS = ("using_filesort",)
_TEMPORARY_KEYS = ("using_temporary_table", "using_temporary")


def _number(value: Any) -> float | None:
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _tables(node: Any, filesort: bool = False, temporary: bool = False) -> Iterator[PlanRow]:
    if isinstance(node, list):
        for item in node:
            yield from _tables(item, filesort, temporary)
        return
    if not isinstance(node, Mapping):
        return
    filesort = filesort or any(node.get(k) is True for k in _FILESORT_KEYS)
    temporary = temporary or any(node.get(k) is True for k in _TEMPORARY_KEYS)
    for key, value in node.items():
        if key == "table" and isinstance(value, Mapping):
            rows = _number(value.get("rows_examined_per_scan"))
            yield PlanRow(
                table=str(value.get("table_name", "?")),
                access_type=str(value.get("access_type", "?")),
                key=value.get("key"),
                rows=int(rows) if rows is not None else None,
                filtered=_number(value.get("filtered")),
                using_filesort=filesort,
                using_temporary=temporary,
            )
            yield from _tables({k: v for k, v in value.items() if k != "table_name"}, filesort, temporary)
        elif isinstance(value, (Mapping, list)):
            yield from _tables(value, filesort, temporary)


def parse_plan(document: Mapping[str, Any]) -> Plan:
    block = document.get("query_block", {})
    cost = _number(block.get("cost_info", {}).get("query_cost")) if isinstance(block, Mapping) else None
    return Plan(rows=tuple(_tables(document)), cost=cost, raw=document)


def explainable(sql: str) -> bool:
    return snailtrail.fingerprint(sql).kind == "SELECT"


class MySQLExplainer:
    def __init__(self, database: Any) -> None:
        self._database = database

    def explain(self, sql: str, database: str) -> Plan | None:
        if not explainable(sql):
            return None
        try:
            with self._database.cursor(database or None) as cursor:
                cursor.execute("EXPLAIN FORMAT=JSON " + sql)
                row = cursor.fetchone()
        except Exception as error:
            log.warning("EXPLAIN failed for a %s statement: %s", database or "default", error)
            return None
        if not row:
            return None
        text = next(iter(row.values())) if isinstance(row, Mapping) else row[0]
        return parse_plan(json.loads(text))
