import json
import threading
import time

import pytest

import snailtrail


def test_version_matches_the_package():
    assert snailtrail.__version__ == "1.0.0"


def test_fingerprint_normalizes_values_and_formatting():
    a = snailtrail.fingerprint("SELECT * FROM `Orders` WHERE id IN (1, 2, 3) -- note")
    b = snailtrail.fingerprint("select *   from orders where id in (4)")
    assert a.text == "select * from orders where id in(?+)"
    assert a.id == b.id
    assert len(a.id) == 16
    assert a.kind == "SELECT"


def test_parse_tree_draws_the_ast():
    tree = snailtrail.parse_tree("SELECT a FROM t WHERE b = 1")
    assert tree.splitlines()[0] == "SELECT"
    assert "└── WHERE" in tree


def test_parse_errors_carry_the_offset():
    with pytest.raises(snailtrail.ParseError) as caught:
        snailtrail.parse_tree("SELECT a FROM t WHERE")
    assert caught.value.offset == 21
    assert isinstance(caught.value, ValueError)


def test_rules_describe_the_advisor():
    rules = snailtrail.rules()
    assert len(rules) == 15
    ids = {r["id"] for r in rules}
    assert {"ST001", "ST005", "ST014"} <= ids
    implicit = next(r for r in rules if r["name"] == "implicit-conversion")
    assert implicit["needs_schema"] is True


def test_advise_returns_findings_with_fixes(shop_schema):
    findings = snailtrail.advise(
        "SELECT id FROM orders WHERE customer_id = 1 AND status = 'paid' ORDER BY created_at DESC LIMIT 20",
        schema=shop_schema,
    )
    missing = next(f for f in findings if f.rule_id == "ST001")
    assert missing.severity == "warning"
    assert "ADD INDEX idx_orders_customer_id_status_created_at" in missing.suggestion
    assert missing.to_dict()["rule_name"] == "missing-index"


def test_advise_honours_disabled_rules():
    sql = "SELECT * FROM products WHERE id = 1"
    assert any(f.rule_id == "ST013" for f in snailtrail.advise(sql))
    assert not snailtrail.advise(sql, disable=["select-star"])
    with pytest.raises(ValueError):
        snailtrail.advise(sql, disable=["no-such-rule"])


def test_schema_catalog_loads_a_dump(shop_schema):
    assert len(shop_schema) == 6
    assert "orders" in shop_schema.tables
    assert shop_schema.has_table("ORDERS")
    assert ("idx_orders_customer", ["customer_id"]) in shop_schema.indexes("orders")
    assert shop_schema.warnings == []


def test_analyze_text_builds_a_ranked_report(report):
    assert report.events + report.skipped == 4000
    assert report.class_count == len(report.classes) >= 18
    assert report.schema_tables == 6
    times = [c.total_time_us for c in report.classes]
    assert times == sorted(times, reverse=True)
    top = report.classes[0]
    assert top.rank == 1
    assert top.p50_us <= top.p95_us <= top.p99_us <= top.max_time_us
    assert sum(top.latency_decades) == top.calls
    assert top.sample["sql"]
    assert set(top.flags) == {"full_scan", "filesort", "tmp_table", "tmp_table_on_disk"}
    assert report.totals["events"] == report.events
    assert sum(report.findings_by_severity.values()) > 0


def test_every_rendered_format(report):
    parsed = json.loads(report.render("json"))
    assert parsed["tool"]["name"] == "snailtrail"
    assert len(parsed["classes"]) == report.class_count
    assert report.render("markdown").startswith("# 🐌 SnailTrail report")
    assert report.render("text", width=120).startswith("SnailTrail")
    with pytest.raises(ValueError):
        report.render("xml")


def test_analyze_options(generated_log):
    top = snailtrail.analyze_text(generated_log, top=3, sort="calls", advise=False)
    assert len(top.classes) == 3
    assert all(not c.findings for c in top.classes)
    calls = [c.calls for c in top.classes]
    assert calls == sorted(calls, reverse=True)
    none = snailtrail.analyze_text(generated_log, database="analytics")
    assert none.events == 0
    with pytest.raises(ValueError):
        snailtrail.analyze_text(generated_log, sort="speed")
    with pytest.raises(ValueError):
        snailtrail.analyze_text(generated_log, min_severity="loud")


def test_analyze_file_matches_analyze_text(tmp_path, generated_log):
    path = tmp_path / "slow.log"
    path.write_text(generated_log, encoding="utf-8")
    from_file = snailtrail.analyze_file(str(path), advise=False)
    from_text = snailtrail.analyze_text(generated_log, advise=False)
    assert from_file.events == from_text.events
    assert [c.id for c in from_file.classes] == [c.id for c in from_text.classes]
    with pytest.raises(OSError):
        snailtrail.analyze_file(str(tmp_path / "missing.log"))


def test_a_real_mysql_log(samples, shop_schema):
    report = snailtrail.analyze_file(str(samples / "mysql-8.4-slow.log"), schema=shop_schema, database="shop")
    assert report.events > 1900
    assert report.skipped == 5
    top = report.classes[0]
    assert top.label == "SELECT order_items, products"
    assert top.flags["full_scan"] == 1.0
    assert any("idx_order_items_order_id" in f.suggestion for f in top.findings if f.rule_id == "ST001")
    assert top.sample["database"] == "shop"


def test_analysis_releases_the_gil():
    text = snailtrail.generate_log(events=150_000, seed=3)
    ticks = 0
    done = threading.Event()

    def count():
        nonlocal ticks
        while not done.is_set():
            ticks += 1
            time.sleep(0.001)

    counter = threading.Thread(target=count)
    counter.start()
    snailtrail.analyze_text(text, threads=1, advise=False)
    done.set()
    counter.join()
    assert ticks > 5
