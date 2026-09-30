from __future__ import annotations

import logging
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass

import snailtrail

from .config import Settings
from .model import Plan
from .ports import Explainer, HistoryStore, SchemaSource

log = logging.getLogger("snailtrail.dashboard")

Analyze = Callable[..., snailtrail.Report]


class AnalysisInProgress(RuntimeError):
    pass


@dataclass(frozen=True)
class AnalysisResult:
    run_id: int
    events: int
    classes: int
    explained: int
    schema_tables: int
    elapsed_seconds: float


class AnalysisService:
    def __init__(
        self,
        settings: Settings,
        history: HistoryStore,
        schema_source: SchemaSource | None = None,
        explainer: Explainer | None = None,
        analyze: Analyze = snailtrail.analyze_file,
    ) -> None:
        self._settings = settings
        self._history = history
        self._schema_source = schema_source
        self._explainer = explainer
        self._analyze = analyze
        self._lock = threading.Lock()

    @property
    def busy(self) -> bool:
        return self._lock.locked()

    def run(self) -> AnalysisResult:
        if not self._lock.acquire(blocking=False):
            raise AnalysisInProgress("an analysis is already running")
        try:
            return self._run()
        finally:
            self._lock.release()

    def _catalog(self) -> snailtrail.SchemaCatalog | None:
        if self._schema_source is None:
            return None
        try:
            catalog = snailtrail.SchemaCatalog.from_ddl(self._schema_source.ddl())
        except Exception as error:
            log.warning("could not read the schema, advising without it: %s", error)
            return None
        for warning in catalog.warnings:
            log.warning("schema: %s", warning)
        return catalog

    def _plans(self, report: snailtrail.Report) -> dict[str, Plan]:
        if self._explainer is None:
            return {}
        plans: dict[str, Plan] = {}
        for c in report.classes[: self._settings.explain_top]:
            if c.kind != "SELECT":
                continue
            plan = self._explainer.explain(c.sample["sql"], c.sample["database"])
            if plan is not None:
                plans[c.id] = plan
        return plans

    def _run(self) -> AnalysisResult:
        started = time.perf_counter()
        catalog = self._catalog()
        report = self._analyze(
            str(self._settings.slow_log),
            threads=self._settings.threads,
            top=self._settings.top,
            schema=catalog,
            database=self._settings.database_filter,
        )
        plans = self._plans(report)
        run_id = self._history.save(report, plans)
        result = AnalysisResult(
            run_id=run_id,
            events=report.events,
            classes=report.class_count,
            explained=len(plans),
            schema_tables=report.schema_tables,
            elapsed_seconds=time.perf_counter() - started,
        )
        log.info(
            "run %d: %d events, %d query classes, %d plans in %.2fs",
            result.run_id,
            result.events,
            result.classes,
            result.explained,
            result.elapsed_seconds,
        )
        return result
