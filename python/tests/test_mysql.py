import os
import uuid

import pytest

import snailtrail
from snailtrail.dashboard import MemoryHistory, MySQLDsn
from snailtrail.dashboard.explain import MySQLExplainer, parse_plan

pytestmark = pytest.mark.mysql

DSN = os.environ.get("SNAILTRAIL_TEST_MYSQL_DSN")
if not DSN:
    pytest.skip("set SNAILTRAIL_TEST_MYSQL_DSN to run the MySQL tests", allow_module_level=True)

from snailtrail.dashboard.mysql import Database, Migrator, MySQLHistory, MySQLSchemaSource  # noqa: E402


def slow_log(ms: float, repeat: int = 20) -> str:
    event = (
        "# Time: 2024-01-15T10:00:00.000000Z\n# User@Host: app[app] @ h []  Id: 1\n"
        "# Query_time: {:.6f}  Lock_time: 0.000001 Rows_sent: 1  Rows_examined: 10\n"
        "SET timestamp=1705312800;\nSELECT name FROM products WHERE sku = 'X-{}';\n"
        "# Time: 2024-01-15T10:00:00.000000Z\n# User@Host: app[app] @ h []  Id: 1\n"
        "# Query_time: 0.002000  Lock_time: 0.000001 Rows_sent: 1  Rows_examined: 1\n"
        "SET timestamp=1705312800;\nSELECT * FROM customers WHERE id = {};\n"
    )
    return "use shop;\n" + "".join(event.format(ms / 1000, i, i) for i in range(repeat))


@pytest.fixture()
def database():
    base = MySQLDsn.parse(DSN)
    name = f"snailtrail_test_{uuid.uuid4().hex[:8]}"
    db = Database(base.with_database(name))
    db.create_database_if_missing()
    yield db
    with Database(base).cursor(None) as cursor:
        cursor.execute(f"DROP DATABASE `{name}`")


@pytest.fixture()
def history(database):
    Migrator(database).apply()
    return MySQLHistory(database)


def test_migrations_apply_once(database):
    assert Migrator(database).apply() == ["001_history"]
    assert Migrator(database).apply() == []
    assert database.ping()


def test_a_saved_report_reads_back_like_the_memory_store(history, report):
    plan = parse_plan({"query_block": {"table": {"table_name": "orders", "access_type": "ALL"}}})
    plans = {report.classes[0].id: plan}
    run_id = history.save(report, plans)
    memory = MemoryHistory()
    memory.save(report, plans)

    run = history.run(run_id)
    assert run.events == report.events
    assert run.class_count == report.class_count
    assert run.critical + run.warning + run.info == sum(report.findings_by_severity.values())
    assert history.latest_run().id == run_id

    stored = history.classes(run_id)
    expected = memory.classes(1)
    assert [s.digest for s in stored] == [s.digest for s in expected]
    for a, b in zip(stored, expected, strict=True):
        assert a.calls == b.calls
        assert a.p95_us == b.p95_us
        assert a.latency_decades == b.latency_decades
        assert a.sample_sql == b.sample_sql
        assert sorted(f.rule_id for f in a.findings) == sorted(f.rule_id for f in b.findings)
    assert stored[0].plan.rows[0].table == "orders"
    assert history.snapshot(run_id, stored[0].digest).rank == 1
    assert history.snapshot(run_id, "0000000000000000") is None
    assert {f.rule_id for _, f in history.findings(run_id, "ST001")} <= {"ST001"}


def test_window_functions_find_the_same_changes_as_python(history):
    memory = MemoryHistory()
    for ms in (10, 10, 45, 2):
        report = snailtrail.analyze_text(slow_log(ms), advise=False)
        history.save(report)
        memory.save(report)
    runs = [r.id for r in history.runs()]
    latest, before = runs[0], runs[1]

    slower, faster = history.changes(before, 1.5)
    assert [c.label for c in slower] == ["SELECT products"]
    assert slower[0].ratio == pytest.approx(4.5, rel=1e-3)
    assert faster == []
    slower, faster = history.changes(latest, 1.5)
    assert slower == []
    assert [round(c.ratio, 3) for c in faster] == [round(2 / 45, 3)]

    mem_slower, _ = memory.changes(3, 1.5)
    assert [c.digest for c in mem_slower] == [c.digest for c in history.changes(before, 1.5)[0]]
    points = history.history(slower[0].digest if slower else faster[0].digest)
    assert len(points) == 4


def test_schema_source_reads_show_create_table(database):
    with database.cursor() as cursor:
        cursor.execute("CREATE TABLE products (id INT PRIMARY KEY, sku VARCHAR(20), KEY idx_sku (sku))")
        cursor.execute("CREATE TABLE customers (id INT PRIMARY KEY, phone VARCHAR(20))")
    catalog = snailtrail.SchemaCatalog.from_ddl(MySQLSchemaSource(database, [database.dsn.database]).ddl())
    assert sorted(catalog.tables) == ["customers", "products"]
    assert ("idx_sku", ["sku"]) in catalog.indexes("products")
    findings = snailtrail.advise("SELECT id FROM customers WHERE phone = 5511", schema=catalog)
    assert any(f.rule_id == "ST005" for f in findings)


def test_explainer_reads_the_plan(database):
    with database.cursor() as cursor:
        cursor.execute("CREATE TABLE products (id INT PRIMARY KEY, sku VARCHAR(20))")
    explainer = MySQLExplainer(database)
    plan = explainer.explain("SELECT * FROM products WHERE sku = 'A' ORDER BY id DESC", database.dsn.database)
    assert plan is not None
    assert plan.rows[0].table == "products"
    assert plan.rows[0].access_type in {"ALL", "index"}
    assert plan.rows[0].full_scan
    assert explainer.explain("DELETE FROM products", database.dsn.database) is None
    assert explainer.explain("SELECT * FROM missing_table", database.dsn.database) is None
