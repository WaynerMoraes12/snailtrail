from __future__ import annotations

import asyncio
import logging
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager, suppress
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, Request
from fastapi.encoders import jsonable_encoder
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse, Response
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from starlette.concurrency import run_in_threadpool

import snailtrail

from . import views
from .config import Settings
from .memory import MemoryHistory
from .ports import HistoryStore
from .service import AnalysisInProgress, AnalysisService

log = logging.getLogger("snailtrail.dashboard")
HERE = Path(__file__).resolve().parent


@dataclass
class Services:
    settings: Settings
    history: HistoryStore
    analysis: AnalysisService
    last_error: str | None = None


def build_services(settings: Settings) -> Services:
    if settings.in_memory:
        history: HistoryStore = MemoryHistory()
    else:
        from .mysql import Database, Migrator, MySQLHistory

        database = Database(settings.history)
        database.create_database_if_missing()
        Migrator(database).apply()
        history = MySQLHistory(database)

    schema_source = explainer = None
    if settings.target is not None:
        from .explain import MySQLExplainer
        from .mysql import Database, MySQLSchemaSource

        target = Database(settings.target)
        schemas = [settings.database_filter or settings.target.database]
        schema_source = MySQLSchemaSource(target, [s for s in schemas if s])
        explainer = MySQLExplainer(target) if settings.explain else None
    return Services(settings, history, AnalysisService(settings, history, schema_source, explainer))


def _templates() -> Jinja2Templates:
    templates = Jinja2Templates(directory=str(HERE / "templates"))
    env = templates.env
    for name in ("duration", "compact", "count", "percent", "when", "data_size", "pretty_sql", "ratio_label"):
        env.filters[name] = getattr(views, name)
    env.globals["version"] = snailtrail.__version__
    env.globals["decade_bars"] = views.decade_bars
    env.globals["fix_parts"] = views.fix_parts
    return templates


def create_app(settings: Settings | None = None, services: Services | None = None) -> FastAPI:
    settings = settings or (services.settings if services else Settings.from_env())
    services = services or build_services(settings)
    templates = _templates()

    async def analyze_now() -> int | None:
        try:
            result = await run_in_threadpool(services.analysis.run)
        except AnalysisInProgress:
            return None
        except Exception as error:
            services.last_error = f"{type(error).__name__}: {error}"
            log.warning("analysis failed: %s", services.last_error)
            raise
        services.last_error = None
        return result.run_id

    async def periodic(interval: int) -> None:
        while True:
            with suppress(Exception):
                await analyze_now()
            await asyncio.sleep(interval)

    @asynccontextmanager
    async def lifespan(_: FastAPI) -> AsyncIterator[None]:
        tasks = []
        if settings.interval_seconds > 0:
            tasks.append(asyncio.create_task(periodic(settings.interval_seconds)))
        elif settings.analyze_on_start:
            tasks.append(asyncio.create_task(_quietly(analyze_now())))
        yield
        for task in tasks:
            task.cancel()

    app = FastAPI(
        title="SnailTrail",
        version=snailtrail.__version__,
        description="MySQL slow-query analysis history, findings and fixes",
        lifespan=lifespan,
    )
    app.state.services = services
    app.mount("/static", StaticFiles(directory=str(HERE / "static")), name="static")

    def page(request: Request, name: str, status_code: int = 200, **context: Any) -> HTMLResponse:
        context.setdefault("busy", services.analysis.busy)
        context.setdefault("last_error", services.last_error)
        context.setdefault("settings", settings)
        return templates.TemplateResponse(request, name, context, status_code=status_code)

    def require_run(run_id: int) -> Any:
        run = services.history.run(run_id)
        if run is None:
            raise HTTPException(status_code=404, detail=f"run {run_id} not found")
        return run

    @app.get("/", response_class=HTMLResponse, include_in_schema=False)
    def home(request: Request) -> Response:
        latest = services.history.latest_run()
        if latest is None:
            return page(request, "empty.html")
        return RedirectResponse(f"/runs/{latest.id}", status_code=307)

    @app.get("/runs", response_class=HTMLResponse, include_in_schema=False)
    def runs_page(request: Request) -> HTMLResponse:
        runs = services.history.runs(100)
        longest = max((r.total_query_time_us for r in runs), default=0)
        return page(request, "runs.html", runs=runs, longest=longest)

    @app.get("/runs/{run_id}", response_class=HTMLResponse, include_in_schema=False)
    def run_page(request: Request, run_id: int) -> HTMLResponse:
        run = require_run(run_id)
        classes = services.history.classes(run_id)
        regressions, improvements = services.history.changes(run_id, settings.regression_threshold)
        latest = services.history.latest_run()
        return page(
            request,
            "run.html",
            run=run,
            classes=classes,
            regressions=regressions,
            improvements=improvements,
            is_latest=latest is not None and latest.id == run.id,
        )

    @app.get("/runs/{run_id}/classes/{digest}", response_class=HTMLResponse, include_in_schema=False)
    def class_page(request: Request, run_id: int, digest: str) -> HTMLResponse:
        run = require_run(run_id)
        snapshot = services.history.snapshot(run_id, digest)
        if snapshot is None:
            raise HTTPException(status_code=404, detail=f"query class {digest} not found in run {run_id}")
        history = services.history.history(digest)
        chart = None
        if len(history) > 1:
            chart = views.line_chart(
                [("avg", [p.avg_time_us for p in history]), ("p95", [float(p.p95_us) for p in history])],
                [f"#{p.run_id}" for p in history],
            )
        return page(request, "class.html", run=run, c=snapshot, history=history, chart=chart)

    @app.get("/rules", response_class=HTMLResponse, include_in_schema=False)
    def rules_page(request: Request) -> HTMLResponse:
        return page(request, "rules.html", rules=snailtrail.rules())

    @app.post("/analyze", include_in_schema=False)
    async def analyze_form() -> Response:
        try:
            run_id = await analyze_now()
        except Exception:
            run_id = None
        if run_id is None:
            return RedirectResponse("/", status_code=303)
        return RedirectResponse(f"/runs/{run_id}", status_code=303)

    @app.get("/api/health", tags=["api"])
    def health() -> dict[str, Any]:
        return {"status": "ok", "version": snailtrail.__version__, "busy": services.analysis.busy}

    @app.get("/api/runs", tags=["api"])
    def api_runs(limit: int = 50) -> list[dict[str, Any]]:
        return jsonable_encoder([asdict(r) for r in services.history.runs(limit)])

    @app.get("/api/runs/{run_id}", tags=["api"])
    def api_run(run_id: int) -> dict[str, Any]:
        run = require_run(run_id)
        regressions, improvements = services.history.changes(run_id, settings.regression_threshold)
        return jsonable_encoder(
            {
                "run": asdict(run),
                "classes": [asdict(c) for c in services.history.classes(run_id)],
                "regressions": [asdict(c) | {"ratio": c.ratio} for c in regressions],
                "improvements": [asdict(c) | {"ratio": c.ratio} for c in improvements],
            }
        )

    @app.get("/api/classes/{digest}/history", tags=["api"])
    def api_history(digest: str, limit: int = 30) -> list[dict[str, Any]]:
        return jsonable_encoder([asdict(p) for p in services.history.history(digest, limit)])

    @app.post("/api/analyze", tags=["api"])
    async def api_analyze() -> JSONResponse:
        try:
            run_id = await analyze_now()
        except Exception as error:
            return JSONResponse({"error": str(error)}, status_code=500)
        if run_id is None:
            return JSONResponse({"error": "an analysis is already running"}, status_code=409)
        return JSONResponse({"run_id": run_id}, status_code=201)

    return app


async def _quietly(awaitable: Any) -> None:
    with suppress(Exception):
        await awaitable
