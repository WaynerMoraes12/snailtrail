from __future__ import annotations

import hashlib
import logging
import random
import threading
import time
from collections import Counter
from collections.abc import Callable
from dataclasses import dataclass, field
from typing import Any

from .seed import Scale

log = logging.getLogger("snailtrail.lab")

Cursor = Any
Step = Callable[[Cursor, random.Random, Scale], None]

STATUSES = ("pending", "paid", "shipped", "delivered", "cancelled")
TERMS = ("phone", "case", "usb", "cable", "shoe", "lamp", "book", "chair", "mouse", "desk", "bag", "watch")
COUNTRIES = ("BR", "US", "PT", "AR", "MX", "DE")


def session_token(n: int) -> str:
    return hashlib.sha256(f"session-{n}".encode()).hexdigest()


def moment(rng: random.Random) -> str:
    day = f"2024-{rng.randint(1, 12):02d}-{rng.randint(1, 28):02d}"
    return f"{day} {rng.randint(0, 23):02d}:{rng.randint(0, 59):02d}:00"


def product_page(c: Cursor, r: random.Random, s: Scale) -> None:
    sql = "SELECT * FROM products WHERE id = %s" if r.random() < 0.5 else "SELECT * FROM `products` WHERE `id` = %s"
    c.execute(sql, (r.randint(1, s.products),))


def session_check(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute("SELECT customer_id FROM sessions WHERE token = %s", (session_token(r.randint(1, s.sessions)),))


def customer_orders(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute(
        "SELECT id, total, created_at FROM orders WHERE customer_id = %s AND status = %s "
        "ORDER BY created_at DESC LIMIT 20",
        (r.randint(1, s.customers), r.choice(STATUSES)),
    )


def order_items(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute(
        "SELECT oi.product_id, oi.quantity, p.name\nFROM order_items oi\n"
        "JOIN products p ON p.id = oi.product_id\nWHERE oi.order_id = %s",
        (r.randint(1, s.orders),),
    )


def place_order(c: Cursor, r: random.Random, s: Scale) -> None:
    at = moment(r)
    c.execute(
        "INSERT INTO orders (customer_id, status, total, created_at, updated_at) VALUES (%s, 'pending', %s, %s, %s)",
        (r.randint(1, s.customers), round(r.uniform(5, 900), 2), at, at),
    )


def add_items(c: Cursor, r: random.Random, s: Scale) -> None:
    rows = [
        (r.randint(1, s.orders), r.randint(1, s.products), r.randint(1, 4), round(r.uniform(2, 300), 2))
        for _ in range(r.randint(1, 5))
    ]
    placeholders = ", ".join(["(%s, %s, %s, %s)"] * len(rows))
    c.execute(
        f"INSERT INTO order_items (order_id, product_id, quantity, unit_price) VALUES {placeholders}",
        [v for row in rows for v in row],
    )


def login(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute(
        "SELECT id, full_name, tier FROM customers WHERE LOWER(email) = %s",
        (f"user{r.randint(1, s.customers)}@example.com",),
    )


def update_status(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute(
        "UPDATE orders SET status = %s, updated_at = %s WHERE id = %s",
        (r.choice(STATUSES), moment(r), r.randint(1, s.orders)),
    )


def search(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute(
        "SELECT id, name, price FROM products WHERE name LIKE %s ORDER BY price LIMIT 50",
        (f"%{r.choice(TERMS)}%",),
    )


def reviews_page(c: Cursor, r: random.Random, s: Scale) -> None:
    page = r.randint(0, 4) if r.random() < 0.7 else r.randint(50, 400)
    c.execute(
        "SELECT * FROM reviews WHERE product_id = %s ORDER BY created_at DESC LIMIT %s, 20",
        (r.randint(1, s.products), page * 20),
    )


def phone_lookup(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute("SELECT id, full_name FROM customers WHERE phone = %s", (5511900000000 + r.randint(1, s.customers),))


def featured(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute("SELECT id, name, price FROM products ORDER BY RAND() LIMIT 8")


def email_or_phone(c: Cursor, r: random.Random, s: Scale) -> None:
    n = r.randint(1, s.customers)
    c.execute(
        "SELECT id FROM customers WHERE email = %s OR phone = %s",
        (f"user{n}@example.com", f"55119{n:08d}"),
    )


def stock_check(c: Cursor, r: random.Random, s: Scale) -> None:
    ids = [r.randint(1, s.products) for _ in range(r.randint(250, 400))]
    c.execute(f"SELECT id, stock FROM products WHERE id IN ({', '.join(['%s'] * len(ids))})", ids)


def daily_revenue(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute(
        "SELECT DATE(created_at) AS day, SUM(total) AS revenue FROM orders "
        "WHERE DATE(created_at) >= %s GROUP BY DATE(created_at)",
        (f"2024-{r.randint(1, 12):02d}-01",),
    )


def categories(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute("SELECT category, COUNT(*) FROM products GROUP BY category HAVING category <> 'books'")


def purge_sessions(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute("DELETE FROM sessions WHERE expires_at < %s", ("2000-01-01 00:00:00",))


def inactive_customers(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute("SELECT id, email FROM customers WHERE id NOT IN (SELECT customer_id FROM orders) LIMIT 100")


def coupon_report(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute(
        "SELECT c.id, o.id FROM customers c, orders o WHERE c.country = %s AND o.coupon_code = %s LIMIT 50",
        (r.choice(COUNTRIES), "WELCOME10"),
    )


def reset_tiers(c: Cursor, r: random.Random, s: Scale) -> None:
    c.execute("UPDATE customers SET tier = 1")


@dataclass(frozen=True)
class Scenario:
    name: str
    weight: float
    step: Step


SCENARIOS: tuple[Scenario, ...] = (
    Scenario("product page", 30, product_page),
    Scenario("session check", 20, session_check),
    Scenario("customer orders", 12, customer_orders),
    Scenario("order items", 10, order_items),
    Scenario("place order", 8, place_order),
    Scenario("add items", 8, add_items),
    Scenario("login", 8, login),
    Scenario("update status", 6, update_status),
    Scenario("search", 6, search),
    Scenario("reviews page", 5, reviews_page),
    Scenario("phone lookup", 4, phone_lookup),
    Scenario("featured", 3, featured),
    Scenario("email or phone", 2, email_or_phone),
    Scenario("stock check", 1, stock_check),
    Scenario("daily revenue", 1, daily_revenue),
    Scenario("categories", 0.5, categories),
    Scenario("purge sessions", 0.5, purge_sessions),
    Scenario("inactive customers", 0.3, inactive_customers),
    Scenario("coupon report", 0.2, coupon_report),
    Scenario("reset tiers", 0.05, reset_tiers),
)


@dataclass
class WorkloadStats:
    executed: Counter[str] = field(default_factory=Counter)
    errors: Counter[str] = field(default_factory=Counter)
    seconds: float = 0.0

    @property
    def total(self) -> int:
        return sum(self.executed.values())


class Workload:
    def __init__(
        self,
        connect: Callable[[], Any],
        scale: Scale,
        seed: int = 7,
        scenarios: tuple[Scenario, ...] = SCENARIOS,
    ) -> None:
        self._connect = connect
        self._scale = scale
        self._seed = seed
        self._scenarios = scenarios
        self._lock = threading.Lock()

    def _worker(self, index: int, iterations: int, stats: WorkloadStats) -> None:
        rng = random.Random(self._seed * 1000 + index)
        weights = [s.weight for s in self._scenarios]
        connection = self._connect()
        try:
            with connection.cursor() as cursor:
                for _ in range(iterations):
                    scenario = rng.choices(self._scenarios, weights)[0]
                    try:
                        scenario.step(cursor, rng, self._scale)
                        cursor.fetchall()
                        connection.commit()
                        outcome = stats.executed
                    except Exception as error:
                        connection.rollback()
                        log.warning("%s failed: %s", scenario.name, error)
                        outcome = stats.errors
                    with self._lock:
                        outcome[scenario.name] += 1
        finally:
            connection.close()

    def run(self, iterations: int, concurrency: int = 4) -> WorkloadStats:
        stats = WorkloadStats()
        started = time.perf_counter()
        share, rest = divmod(iterations, concurrency)
        threads = [
            threading.Thread(target=self._worker, args=(i, share + (1 if i < rest else 0), stats))
            for i in range(concurrency)
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        stats.seconds = time.perf_counter() - started
        return stats
