from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any

import snailtrail

SEVERITIES = ("critical", "warning", "info")


@dataclass(frozen=True)
class FindingRow:
    rule_id: str
    rule_name: str
    severity: str
    title: str
    detail: str
    suggestion: str


@dataclass(frozen=True)
class PlanRow:
    table: str
    access_type: str
    key: str | None
    rows: int | None
    filtered: float | None
    using_filesort: bool = False
    using_temporary: bool = False

    @property
    def full_scan(self) -> bool:
        return self.access_type in {"ALL", "index"}


@dataclass(frozen=True)
class Plan:
    rows: tuple[PlanRow, ...]
    cost: float | None
    raw: Mapping[str, Any]

    @property
    def full_scans(self) -> tuple[PlanRow, ...]:
        return tuple(r for r in self.rows if r.full_scan)


@dataclass(frozen=True)
class RunSummary:
    id: int
    created_at: datetime
    source: str
    log_bytes: int
    events: int
    skipped: int
    class_count: int
    total_query_time_us: int
    total_rows_examined: int
    window_start: datetime | None
    window_end: datetime | None
    threads: int
    parse_seconds: float
    schema_tables: int
    critical: int
    warning: int
    info: int

    @property
    def findings(self) -> int:
        return self.critical + self.warning + self.info


@dataclass(frozen=True)
class ClassSnapshot:
    run_id: int
    digest: str
    rank: int
    kind: str
    label: str
    fingerprint: str
    calls: int
    time_share: float
    total_time_us: int
    avg_time_us: float
    p50_us: int
    p95_us: int
    p99_us: int
    max_us: int
    lock_time_us: int
    rows_examined: int
    rows_sent: int
    full_scan_ratio: float
    filesort_ratio: float
    tmp_disk_ratio: float
    latency_decades: tuple[int, ...]
    sample_sql: str
    sample_database: str
    sample_time_us: int
    plan: Plan | None = None
    findings: tuple[FindingRow, ...] = field(default_factory=tuple)

    @property
    def rows_examined_per_call(self) -> float:
        return self.rows_examined / self.calls if self.calls else 0.0

    @property
    def worst_severity(self) -> str | None:
        for severity in SEVERITIES:
            if any(f.severity == severity for f in self.findings):
                return severity
        return None


@dataclass(frozen=True)
class ChangePolicy:
    threshold: float = 1.5
    min_delta_us: float = 1000.0
    min_calls: int = 10

    def significant(self, before_us: float, after_us: float, before_calls: int, after_calls: int) -> bool:
        if before_us <= 0 or after_us <= 0:
            return False
        if before_calls < self.min_calls or after_calls < self.min_calls:
            return False
        if abs(after_us - before_us) < self.min_delta_us:
            return False
        ratio = after_us / before_us
        return ratio >= self.threshold or ratio <= 1 / self.threshold


@dataclass(frozen=True)
class HistoryPoint:
    run_id: int
    created_at: datetime
    calls: int
    total_time_us: int
    avg_time_us: float
    p95_us: int


@dataclass(frozen=True)
class Change:
    digest: str
    label: str
    previous_avg_us: float
    current_avg_us: float

    @property
    def ratio(self) -> float:
        return self.current_avg_us / self.previous_avg_us if self.previous_avg_us else float("inf")


def _timestamp(value: int) -> datetime | None:
    return datetime.fromtimestamp(value, tz=timezone.utc).replace(tzinfo=None) if value else None


def run_fields(report: snailtrail.Report) -> dict[str, Any]:
    counts = report.findings_by_severity
    return {
        "source": report.source,
        "log_bytes": report.bytes,
        "events": report.events,
        "skipped": report.skipped,
        "class_count": report.class_count,
        "total_query_time_us": report.totals["query_time_us"],
        "total_rows_examined": report.totals["rows_examined"],
        "window_start": _timestamp(report.totals["first_seen"]),
        "window_end": _timestamp(report.totals["last_seen"]),
        "threads": report.threads,
        "parse_seconds": report.parse_seconds,
        "schema_tables": report.schema_tables,
        "critical": counts["critical"],
        "warning": counts["warning"],
        "info": counts["info"],
    }


def snapshots_from_report(
    report: snailtrail.Report, run_id: int, plans: Mapping[str, Plan] | None = None
) -> list[ClassSnapshot]:
    plans = plans or {}
    snapshots = []
    for c in report.classes:
        flags = c.flags
        sample = c.sample
        snapshots.append(
            ClassSnapshot(
                run_id=run_id,
                digest=c.id,
                rank=c.rank,
                kind=c.kind,
                label=c.label[:255],
                fingerprint=c.fingerprint,
                calls=c.calls,
                time_share=c.time_share,
                total_time_us=c.total_time_us,
                avg_time_us=c.avg_time_us,
                p50_us=c.p50_us,
                p95_us=c.p95_us,
                p99_us=c.p99_us,
                max_us=c.max_time_us,
                lock_time_us=c.lock_time_us,
                rows_examined=c.rows_examined,
                rows_sent=c.rows_sent,
                full_scan_ratio=flags["full_scan"],
                filesort_ratio=flags["filesort"],
                tmp_disk_ratio=flags["tmp_table_on_disk"],
                latency_decades=tuple(c.latency_decades),
                sample_sql=sample["sql"],
                sample_database=sample["database"][:64],
                sample_time_us=sample["query_time_us"],
                plan=plans.get(c.id),
                findings=tuple(
                    FindingRow(f.rule_id, f.rule_name, f.severity, f.title[:512], f.detail, f.suggestion)
                    for f in c.findings
                ),
            )
        )
    return snapshots


def compare(
    previous: Mapping[str, ClassSnapshot], current: list[ClassSnapshot], policy: ChangePolicy
) -> tuple[list[Change], list[Change]]:
    regressions: list[Change] = []
    improvements: list[Change] = []
    for snap in current:
        before = previous.get(snap.digest)
        if before is None or not policy.significant(before.avg_time_us, snap.avg_time_us, before.calls, snap.calls):
            continue
        change = Change(snap.digest, snap.label, before.avg_time_us, snap.avg_time_us)
        if change.ratio >= policy.threshold:
            regressions.append(change)
        else:
            improvements.append(change)
    regressions.sort(key=lambda c: c.ratio, reverse=True)
    improvements.sort(key=lambda c: c.ratio)
    return regressions, improvements
