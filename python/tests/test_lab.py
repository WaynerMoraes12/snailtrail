import os
import random
import uuid

import pytest

import snailtrail
from snailtrail.lab import SCALES, SCENARIOS, Seeder, Workload, index_statements
from snailtrail.lab.workload import session_token


def literal(value):
    if isinstance(value, (int, float)):
        return str(value)
    return "'" + str(value).replace("'", "''") + "'"


class FakeCursor:
    def __init__(self):
        self.statements = []

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    def execute(self, sql, args=None):
        if args is not None:
            parts = sql.split("%s")
            sql = parts[0] + "".join(literal(a) + p for a, p in zip(args, parts[1:], strict=True))
        self.statements.append(sql)

    def fetchall(self):
        return []


class FakeConnection:
    def __init__(self, log):
        self._log = log

    def cursor(self):
        cursor = FakeCursor()
        self._log.append(cursor)
        return cursor

    def commit(self):
        pass

    def rollback(self):
        pass

    def close(self):
        pass


def statements_of(name, seed=3):
    scenario = next(s for s in SCENARIOS if s.name == name)
    cursor = FakeCursor()
    scenario.step(cursor, random.Random(seed), SCALES["small"])
    return cursor.statements


def rules_for(sql, schema=None):
    return {f.rule_id for f in snailtrail.advise(sql, schema=schema)}


def test_scenarios_are_well_formed():
    assert len(SCENARIOS) == 20
    assert len({s.name for s in SCENARIOS}) == 20
    assert all(s.weight > 0 for s in SCENARIOS)
    assert len(session_token(1)) == 64
    assert session_token(1) != session_token(2)


@pytest.mark.parametrize(
    ("scenario", "rule"),
    [
        ("login", "ST004"),
        ("daily revenue", "ST004"),
        ("search", "ST006"),
        ("inactive customers", "ST007"),
        ("featured", "ST009"),
        ("email or phone", "ST010"),
        ("stock check", "ST011"),
        ("categories", "ST012"),
        ("product page", "ST013"),
        ("coupon report", "ST003"),
        ("reset tiers", "ST002"),
        ("order items", "ST001"),
        ("customer orders", "ST001"),
        ("purge sessions", "ST001"),
    ],
)
def test_each_problem_scenario_triggers_its_rule(scenario, rule, shop_schema):
    (sql,) = statements_of(scenario)
    assert rule in rules_for(sql, shop_schema)


def test_the_phone_lookup_needs_the_schema(shop_schema):
    (sql,) = statements_of("phone lookup")
    assert "ST005" in rules_for(sql, shop_schema)
    assert "ST005" not in rules_for(sql)


def test_healthy_scenarios_stay_quiet(shop_schema):
    for name in ("session check", "update status", "place order"):
        (sql,) = statements_of(name)
        assert rules_for(sql, shop_schema) == set(), name


def test_workload_spreads_iterations_over_workers():
    cursors = []
    stats = Workload(lambda: FakeConnection(cursors), SCALES["tiny"], seed=1).run(250, concurrency=3)
    assert stats.total == 250
    assert not stats.errors
    assert len(cursors) == 3
    assert sum(len(c.statements) for c in cursors) == 250
    assert stats.executed["product page"] > stats.executed["reset tiers"]


def test_index_statements_come_from_missing_index_findings(report):
    statements = index_statements(report)
    assert "ALTER TABLE order_items ADD INDEX idx_order_items_order_id (order_id);" in statements
    assert all(s.startswith("ALTER TABLE") and "ADD INDEX" in s for s in statements)
    assert len(statements) == len(set(statements))


@pytest.mark.mysql
def test_seed_and_run_against_mysql(samples):
    dsn = os.environ.get("SNAILTRAIL_TEST_MYSQL_DSN")
    if not dsn:
        pytest.skip("set SNAILTRAIL_TEST_MYSQL_DSN to run the MySQL tests")
    from snailtrail.lab.__main__ import connector

    name = f"snailtrail_lab_{uuid.uuid4().hex[:8]}"
    admin = connector(dsn, database="")()
    try:
        with admin.cursor() as cursor:
            cursor.execute(f"CREATE DATABASE `{name}`")
        connect = connector(dsn, database=name)
        connection = connect()
        with connection.cursor() as cursor:
            for statement in (samples / "shop_schema.sql").read_text(encoding="utf-8").split(";"):
                if statement.strip():
                    cursor.execute(statement)
        counts = Seeder(connection, SCALES["tiny"]).seed()
        with connection.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) AS n FROM order_items")
            assert cursor.fetchone()["n"] == SCALES["tiny"].items
            cursor.execute("SELECT token FROM sessions WHERE id = 1")
            assert cursor.fetchone()["token"] == session_token(1)
        connection.close()
        assert counts["customers"] == SCALES["tiny"].customers

        stats = Workload(connect, SCALES["tiny"], seed=5).run(300, concurrency=3)
        assert stats.total == 300
        assert not stats.errors
    finally:
        with admin.cursor() as cursor:
            cursor.execute(f"DROP DATABASE IF EXISTS `{name}`")
        admin.close()
