from __future__ import annotations

import threading
from collections.abc import Mapping
from dataclasses import replace
from datetime import datetime, timezone

import snailtrail

from .model import (
    Change,
    ClassSnapshot,
    FindingRow,
    HistoryPoint,
    Plan,
    RunSummary,
    compare,
    run_fields,
    snapshots_from_report,
)


def _now() -> datetime:
    return datetime.now(timezone.utc).replace(tzinfo=None)


class MemoryHistory:
    def __init__(self) -> None:
        self._runs: dict[int, RunSummary] = {}
        self._classes: dict[int, list[ClassSnapshot]] = {}
        self._lock = threading.Lock()

    def save(self, report: snailtrail.Report, plans: Mapping[str, Plan] | None = None) -> int:
        with self._lock:
            run_id = len(self._runs) + 1
            self._runs[run_id] = RunSummary(id=run_id, created_at=_now(), **run_fields(report))
            self._classes[run_id] = snapshots_from_report(report, run_id, plans)
            return run_id

    def runs(self, limit: int = 50) -> list[RunSummary]:
        return [self._runs[i] for i in sorted(self._runs, reverse=True)[:limit]]

    def run(self, run_id: int) -> RunSummary | None:
        return self._runs.get(run_id)

    def latest_run(self) -> RunSummary | None:
        return self._runs[max(self._runs)] if self._runs else None

    def classes(self, run_id: int) -> list[ClassSnapshot]:
        return list(self._classes.get(run_id, []))

    def snapshot(self, run_id: int, digest: str) -> ClassSnapshot | None:
        return next((s for s in self._classes.get(run_id, []) if s.digest == digest), None)

    def history(self, digest: str, limit: int = 30) -> list[HistoryPoint]:
        points = []
        for run_id in sorted(self._classes):
            snap = self.snapshot(run_id, digest)
            if snap is not None:
                run = self._runs[run_id]
                points.append(
                    HistoryPoint(run_id, run.created_at, snap.calls, snap.total_time_us, snap.avg_time_us, snap.p95_us)
                )
        return points[-limit:]

    def changes(self, run_id: int, threshold: float) -> tuple[list[Change], list[Change]]:
        previous: dict[str, ClassSnapshot] = {}
        for earlier in sorted(i for i in self._classes if i < run_id):
            for snap in self._classes[earlier]:
                previous[snap.digest] = snap
        return compare(previous, self._classes.get(run_id, []), threshold)

    def findings(self, run_id: int, rule_id: str | None = None) -> list[tuple[str, FindingRow]]:
        return [
            (snap.digest, f)
            for snap in self._classes.get(run_id, [])
            for f in snap.findings
            if rule_id is None or f.rule_id == rule_id
        ]

    def restamp(self, run_id: int, created_at: datetime) -> None:
        self._runs[run_id] = replace(self._runs[run_id], created_at=created_at)
