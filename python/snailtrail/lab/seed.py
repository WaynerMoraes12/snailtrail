from __future__ import annotations

import logging
from dataclasses import dataclass
from typing import Any

log = logging.getLogger("snailtrail.lab")


@dataclass(frozen=True)
class Scale:
    customers: int
    products: int
    orders: int
    items_per_order: int
    reviews: int
    sessions: int

    @property
    def items(self) -> int:
        return self.orders * self.items_per_order


SCALES = {
    "tiny": Scale(customers=2_000, products=300, orders=8_000, items_per_order=3, reviews=4_000, sessions=2_000),
    "small": Scale(customers=20_000, products=2_000, orders=80_000, items_per_order=3, reviews=40_000, sessions=20_000),
    "medium": Scale(
        customers=100_000, products=5_000, orders=400_000, items_per_order=3, reviews=200_000, sessions=100_000
    ),
}

TABLES = ("order_items", "reviews", "sessions", "orders", "products", "customers")

_SEQUENCE = "WITH RECURSIVE seq (n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < {count})"

_STATEMENTS = (
    (
        "customers",
        "INSERT INTO customers (email, full_name, phone, country, city, tier, created_at) "
        + _SEQUENCE
        + " SELECT CONCAT('user', n, '@example.com'),"
        " CONCAT(ELT(1 + n % 10, 'Ana', 'Bruno', 'Carla', 'Diego', 'Elis', 'Fabio', 'Gabi', 'Heitor', 'Iris', 'Joao'),"
        " ' ', ELT(1 + (n DIV 10) % 8, 'Silva', 'Souza', 'Lima', 'Costa', 'Rocha', 'Alves', 'Moraes', 'Dias')),"
        " IF(n % 7 = 0, NULL, CONCAT('55119', LPAD(n, 8, '0'))),"
        " ELT(1 + CRC32(n) % 6, 'BR', 'US', 'PT', 'AR', 'MX', 'DE'),"
        " ELT(1 + CRC32(n * 3) % 6, 'Sao Paulo', 'Rio de Janeiro', 'Lisbon', 'Austin', 'Porto', 'Berlin'),"
        " 1 + n % 3, TIMESTAMP('2022-01-01') + INTERVAL (n * 37) % 700 DAY FROM seq",
        "customers",
    ),
    (
        "products",
        "INSERT INTO products (sku, name, category, price, stock, description) "
        + _SEQUENCE
        + " SELECT CONCAT('SKU-', LPAD(n, 6, '0')),"
        " CONCAT(ELT(1 + n % 12, 'Phone', 'Case', 'USB Cable', 'Cable', 'Shoe', 'Lamp', 'Book', 'Chair', 'Mouse',"
        " 'Desk', 'Bag', 'Watch'), ' ', ELT(1 + (n DIV 12) % 6, 'Pro', 'Mini', 'Max', 'Lite', 'Plus', 'Air'), ' ', n),"
        " ELT(1 + n % 8, 'electronics', 'books', 'home', 'fashion', 'sports', 'toys', 'garden', 'office'),"
        " ROUND(5 + (CRC32(n) % 99500) / 100, 2), CRC32(n * 7) % 500,"
        " REPEAT(CONCAT('Description of product ', n, '. '), 1 + n % 8) FROM seq",
        "products",
    ),
    (
        "orders",
        "INSERT INTO orders (customer_id, status, total, coupon_code, created_at, updated_at) "
        + _SEQUENCE
        + " SELECT 1 + (n * 7919) % {customers},"
        " ELT(1 + CRC32(n) % 5, 'pending', 'paid', 'shipped', 'delivered', 'cancelled'),"
        " ROUND(10 + (CRC32(n * 11) % 200000) / 100, 2),"
        " IF(n % 50 = 0, 'WELCOME10', NULL),"
        " TIMESTAMP('2024-01-01') + INTERVAL (n * 13) % 525600 MINUTE,"
        " TIMESTAMP('2024-01-01') + INTERVAL (n * 13) % 525600 MINUTE + INTERVAL 1 HOUR FROM seq",
        "orders",
    ),
    (
        "order_items",
        "INSERT INTO order_items (order_id, product_id, quantity, unit_price) "
        + _SEQUENCE
        + " SELECT 1 + (n - 1) DIV {items_per_order}, 1 + CRC32(n) % {products}, 1 + n % 4,"
        " ROUND(2 + (CRC32(n * 5) % 30000) / 100, 2) FROM seq",
        "items",
    ),
    (
        "reviews",
        "INSERT INTO reviews (product_id, customer_id, rating, body, created_at) "
        + _SEQUENCE
        + " SELECT 1 + CRC32(n * 3) % {products}, 1 + (n * 104729) % {customers}, 1 + CRC32(n) % 5,"
        " CONCAT('Review ', n, ': ', ELT(1 + n % 4, 'great', 'fine', 'meh', 'terrible')),"
        " TIMESTAMP('2023-01-01') + INTERVAL (n * 17) % 900000 MINUTE FROM seq",
        "reviews",
    ),
    (
        "sessions",
        "INSERT INTO sessions (customer_id, token, expires_at) "
        + _SEQUENCE
        + " SELECT 1 + (n * 31) % {customers}, SHA2(CONCAT('session-', n), 256),"
        " TIMESTAMP('2024-06-01') + INTERVAL n MINUTE FROM seq",
        "sessions",
    ),
)


class Seeder:
    def __init__(self, connection: Any, scale: Scale) -> None:
        self._connection = connection
        self._scale = scale

    def seed(self) -> dict[str, int]:
        counts: dict[str, int] = {}
        with self._connection.cursor() as cursor:
            cursor.execute("SET SESSION long_query_time = 3600")
            cursor.execute("SET SESSION cte_max_recursion_depth = 10000000")
            cursor.execute("SET FOREIGN_KEY_CHECKS = 0")
            for table in TABLES:
                cursor.execute(f"TRUNCATE TABLE {table}")
            for table, template, size_field in _STATEMENTS:
                count = getattr(self._scale, size_field)
                cursor.execute(
                    template.format(
                        count=count,
                        customers=self._scale.customers,
                        products=self._scale.products,
                        items_per_order=self._scale.items_per_order,
                    )
                )
                counts[table] = count
                log.info("seeded %s with %d rows", table, count)
            cursor.execute("SET FOREIGN_KEY_CHECKS = 1")
            for table in TABLES:
                cursor.execute(f"ANALYZE TABLE {table}")
                cursor.fetchall()
        self._connection.commit()
        return counts
