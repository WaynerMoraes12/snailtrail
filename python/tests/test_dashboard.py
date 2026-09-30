import json
import threading
from dataclasses import replace
from datetime import datetime

import pytest
from fastapi.testclient import TestClient

import snailtrail
from snailtrail.dashboard import AnalysisInProgress, AnalysisService, MemoryHistory, MySQLDsn, Settings, create_app
from snailtrail.dashboard.app import Services
from snailtrail.dashboard.explain import explainable, parse_plan
from snailtrail.dashboard.model import ChangePolicy, compare, snapshots_from_report
from snailtrail.dashboard.views import compact, decade_bars, duration, fix_parts, line_chart, pretty_sql, ratio_label

EXPLAIN = {
    "query_block": {
        "select_id": 1,
        "cost_info": {"query_cost": "20512.35"},
        "ordering_operation": {
            "using_filesort": True,
            "nested_loop": [
                {
                    "table": {
                        "table_name": "o",
                        "access_type": "ALL",
                        "rows_examined_per_scan": 198731,
                        "filtered": "10.00",
                    }
                },
                {
                    "table": {
                        "table_name": "c",
                        "access_type": "eq_ref",
                        "key": "PRIMARY",
                        "rows_examined_per_scan": 1,
                        "filtered": "100.00",
                    }
                },
            ],
        },
    }
}


class FakeExplainer:
    def __init__(self):
        self.calls = []

    def explain(self, sql, database):
        self.calls.append((sql, database))
        return parse_plan(EXPLAIN)


class FakeSchema:
    def __init__(self, ddl):
        self._ddl = ddl

    def ddl(self):
        return self._ddl


def settings_for(path, **overrides):
    values = {"slow_log": path, "history_dsn": "memory://", "analyze_on_start": False, "explain_top": 3}
    values.update(overrides)
    return Settings(**values)


@pytest.fixture()
def log_file(tmp_path, generated_log):
    path = tmp_path / "slow.log"
    path.write_text(generated_log, encoding="utf-8")
    return path


def test_dsn_parsing_and_redaction():
    dsn = MySQLDsn.parse("mysql://app:s%40cret@db.local:3307/shop")
    assert (dsn.host, dsn.port, dsn.user, dsn.password, dsn.database) == ("db.local", 3307, "app", "s@cret", "shop")
    assert dsn.redacted() == "mysql://app:***@db.local:3307/shop"
    assert dsn.with_database("other").database == "other"
    with pytest.raises(ValueError):
        MySQLDsn.parse("postgres://x@y/z")


def test_settings_from_environment():
    settings = Settings.from_env(
        {
            "SNAILTRAIL_SLOW_LOG": "/logs/slow.log",
            "SNAILTRAIL_TARGET_DSN": "mysql://app:app@mysql/shop",
            "SNAILTRAIL_TOP": "25",
            "SNAILTRAIL_EXPLAIN": "no",
            "SNAILTRAIL_DATABASE": "shop",
        }
    )
    assert str(settings.slow_log) == "/logs/slow.log"
    assert settings.in_memory
    assert settings.target.database == "shop"
    assert settings.top == 25
    assert settings.explain is False
    assert settings.database_filter == "shop"
    with pytest.raises(ValueError):
        Settings.from_env({"SNAILTRAIL_TOP": "many"})


def test_view_helpers():
    assert duration(812) == "812 µs"
    assert duration(45_600) == "45.6 ms"
    assert duration(2_500_000) == "2.50 s"
    assert duration(252_000_000) == "4m 12s"
    assert compact(12_345) == "12.3k"
    assert compact(3) == "3"
    assert ratio_label(4.0) == "4.0× slower"
    assert ratio_label(0.25) == "4.0× faster"
    assert pretty_sql("select a from t where x = ? and y = ?") == "select a\nfrom t\nwhere x = ?\n  and y = ?"
    bars = decade_bars([0, 0, 0, 10, 40, 5, 0, 0])
    assert bars[0].label == "1µs"
    assert max(b.height for b in bars) == 64.0
    chart = line_chart([("avg", [1000.0, 3000.0, 2000.0])], ["#1", "#2", "#3"])
    assert chart.series[0].points.count(",") == 3
    assert chart.labels[-1][1] == "#3"


def test_suggestions_split_into_prose_and_sql():
    parts = fix_parts(
        "ALTER TABLE orders ADD INDEX i (a, b);\n"
        "Then drop the index it makes redundant: ALTER TABLE orders DROP INDEX j;"
    )
    assert [(p.kind, p.text) for p in parts] == [
        ("sql", "ALTER TABLE orders ADD INDEX i (a, b);"),
        ("text", "Then drop the index it makes redundant:"),
        ("sql", "ALTER TABLE orders DROP INDEX j;"),
    ]
    keyset = fix_parts("Use keyset pagination:\n... WHERE id > ? ORDER BY id LIMIT 20\n(add a tie-breaker)")
    assert [p.kind for p in keyset] == ["text", "sql", "text"]
    fulltext = fix_parts(
        "For word search:\nALTER TABLE p ADD FULLTEXT INDEX f (name);\n... WHERE MATCH(name) AGAINST('x')"
    )
    assert [p.kind for p in fulltext] == ["text", "sql"]
    assert fulltext[1].text.count("\n") == 1
    assert [p.kind for p in fix_parts("Quote the value: phone = '5511'")] == ["text"]


def test_explain_plans_are_flattened():
    plan = parse_plan(EXPLAIN)
    assert plan.cost == pytest.approx(20512.35)
    assert [r.table for r in plan.rows] == ["o", "c"]
    assert plan.rows[0].full_scan and plan.rows[0].using_filesort
    assert plan.rows[1].key == "PRIMARY" and not plan.rows[1].full_scan
    assert [r.table for r in plan.full_scans] == ["o"]
    assert explainable("SELECT 1")
    assert not explainable("UPDATE t SET a = 1")
    assert not explainable("WITH x AS (SELECT 1) DELETE FROM t")


def test_memory_history_tracks_runs_and_changes(report):
    history = MemoryHistory()
    first = history.save(report)
    second = history.save(report)
    assert [r.id for r in history.runs()] == [second, first]
    assert history.latest_run().id == second
    classes = history.classes(first)
    assert len(classes) == report.class_count
    top = classes[0]
    assert history.snapshot(first, top.digest).rank == 1
    assert len(history.history(top.digest)) == 2
    assert history.changes(second, ChangePolicy()) == ([], [])
    assert all(f.rule_id == "ST001" for _, f in history.findings(first, "ST001"))
    assert history.run(99) is None


def test_compare_finds_regressions_and_improvements(report):
    before = {s.digest: s for s in snapshots_from_report(report, 1)}
    busy = [s for s in snapshots_from_report(report, 2) if s.calls >= 10]
    slow = replace(busy[0], avg_time_us=busy[0].avg_time_us * 3)
    fast = replace(busy[1], avg_time_us=busy[1].avg_time_us / 4)
    regressions, improvements = compare(before, [slow, fast, *busy[2:]], ChangePolicy())
    assert [c.digest for c in regressions] == [slow.digest]
    assert regressions[0].ratio == pytest.approx(3)
    assert [c.digest for c in improvements] == [fast.digest]


SCHEMA_DDL = "CREATE TABLE orders (id INT PRIMARY KEY, customer_id INT); CREATE TABLE customers (id INT PRIMARY KEY);"


def test_change_policy_ignores_noise():
    policy = ChangePolicy(threshold=1.5, min_delta_us=1000, min_calls=10)
    assert policy.significant(10_000, 30_000, 50, 50)
    assert policy.significant(30_000, 10_000, 50, 50)
    assert not policy.significant(100, 320, 500, 500)
    assert not policy.significant(10_000, 30_000, 5, 50)
    assert not policy.significant(10_000, 13_000, 50, 50)
    assert not policy.significant(0, 13_000, 50, 50)
    assert Settings.from_env({"SNAILTRAIL_REGRESSION_MIN_DELTA_MS": "5"}).change_policy.min_delta_us == 5000


def test_service_analyzes_explains_and_saves(log_file):
    history = MemoryHistory()
    explainer = FakeExplainer()
    service = AnalysisService(settings_for(log_file), history, FakeSchema(SCHEMA_DDL), explainer)
    result = service.run()
    assert result.run_id == 1
    assert result.schema_tables == 2
    assert 0 < result.explained <= 3
    assert all(snailtrail.fingerprint(sql).kind == "SELECT" for sql, _ in explainer.calls)
    snapshots = history.classes(1)
    assert sum(1 for s in snapshots if s.plan is not None) == result.explained
    assert not service.busy


def test_service_survives_a_broken_schema_source(log_file):
    class Broken:
        def ddl(self):
            raise ConnectionError("no route to host")

    result = AnalysisService(settings_for(log_file), MemoryHistory(), Broken()).run()
    assert result.schema_tables == 0


def test_service_refuses_concurrent_runs(log_file):
    started = threading.Event()
    release = threading.Event()

    def slow_analyze(*args, **kwargs):
        started.set()
        release.wait(5)
        return snailtrail.analyze_file(*args, **kwargs)

    service = AnalysisService(settings_for(log_file), MemoryHistory(), analyze=slow_analyze)
    worker = threading.Thread(target=service.run)
    worker.start()
    started.wait(5)
    assert service.busy
    with pytest.raises(AnalysisInProgress):
        service.run()
    release.set()
    worker.join()


@pytest.fixture()
def client(log_file):
    settings = settings_for(log_file)
    history = MemoryHistory()
    services = Services(settings, history, AnalysisService(settings, history, explainer=FakeExplainer()))
    with TestClient(create_app(settings, services)) as test_client:
        yield test_client, history


def test_empty_dashboard_invites_an_analysis(client):
    http, _ = client
    page = http.get("/")
    assert page.status_code == 200
    assert "No analysis yet" in page.text
    assert "Analyze the log now" in page.text


def test_analysis_from_the_form_and_the_pages(client):
    http, history = client
    response = http.post("/analyze", follow_redirects=False)
    assert response.status_code == 303
    assert response.headers["location"] == "/runs/1"

    run = http.get("/runs/1")
    assert run.status_code == 200
    assert "of query time in" in run.text
    assert 'class="ribbon"' in run.text
    assert "Queries by total time" in run.text

    top = history.classes(1)[0]
    detail = http.get(f"/runs/1/classes/{top.digest}")
    assert detail.status_code == 200
    assert "Measured in the log" in detail.text
    assert "EXPLAIN of the slowest execution" in detail.text or top.kind != "SELECT"

    assert http.get("/").status_code == 200
    assert "Every analysis" in http.get("/runs").text
    assert "ST001" in http.get("/rules").text
    assert http.get("/runs/42").status_code == 404
    assert http.get("/runs/1/classes/NOPE").status_code == 404


def test_history_chart_appears_after_two_runs(client):
    http, history = client
    http.post("/api/analyze")
    http.post("/api/analyze")
    digest = history.classes(2)[0].digest
    page = http.get(f"/runs/2/classes/{digest}")
    assert "Across runs" in page.text
    assert "<polyline" in page.text


def test_json_api(client):
    http, _ = client
    assert http.get("/api/health").json()["status"] == "ok"
    created = http.post("/api/analyze")
    assert created.status_code == 201
    run_id = created.json()["run_id"]
    runs = http.get("/api/runs").json()
    assert runs[0]["id"] == run_id
    body = http.get(f"/api/runs/{run_id}").json()
    assert body["run"]["events"] > 0
    first = body["classes"][0]
    assert {"digest", "calls", "p95_us", "findings", "latency_decades"} <= set(first)
    datetime.fromisoformat(body["run"]["created_at"])
    history = http.get(f"/api/classes/{first['digest']}/history").json()
    assert history[0]["run_id"] == run_id
    json.dumps(body)


def test_failed_analysis_is_reported(tmp_path):
    settings = settings_for(tmp_path / "missing.log")
    history = MemoryHistory()
    services = Services(settings, history, AnalysisService(settings, history))
    with TestClient(create_app(settings, services)) as http:
        assert http.post("/api/analyze").status_code == 500
        page = http.get("/")
        assert "The last analysis failed" in page.text
        assert http.post("/analyze", follow_redirects=False).status_code == 303
