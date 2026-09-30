from __future__ import annotations

import argparse
import logging
import os
import random
import sys
import time
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlparse

from .improve import Improver
from .seed import SCALES, Seeder
from .workload import Workload

log = logging.getLogger("snailtrail.lab")


def connector(dsn: str, database: str | None = None) -> Any:
    import pymysql
    import pymysql.cursors

    url = urlparse(dsn)
    options = {
        "host": url.hostname or "localhost",
        "port": url.port or 3306,
        "user": unquote(url.username or "root"),
        "password": unquote(url.password or ""),
        "database": database if database is not None else url.path.lstrip("/"),
        "charset": "utf8mb4",
        "cursorclass": pymysql.cursors.DictCursor,
        "autocommit": True,
    }

    def connect() -> Any:
        return pymysql.connect(**options)

    return connect


def wait_for(connect: Any, seconds: int = 120) -> None:
    deadline = time.monotonic() + seconds
    while True:
        try:
            connect().close()
            return
        except Exception as error:
            if time.monotonic() > deadline:
                raise SystemExit(f"MySQL is not reachable: {error}") from error
            time.sleep(2)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="snailtrail-lab", description="Seed the shop database and replay a workload")
    parser.add_argument(
        "--dsn", default=os.environ.get("SNAILTRAIL_LAB_DSN", "mysql://root:snailtrail@127.0.0.1:3307/shop")
    )
    parser.add_argument("--slow-log", type=Path, default=Path(os.environ.get("SNAILTRAIL_LAB_SLOW_LOG", "slow.log")))
    commands = parser.add_subparsers(dest="command", required=True)

    seed = commands.add_parser("seed", help="fill the shop tables with generated data")
    seed.add_argument("--scale", choices=sorted(SCALES), default=os.environ.get("SNAILTRAIL_LAB_SCALE", "small"))

    run = commands.add_parser("run", help="replay the shop workload")
    run.add_argument("--scale", choices=sorted(SCALES), default=os.environ.get("SNAILTRAIL_LAB_SCALE", "small"))
    run.add_argument("--iterations", type=int, default=int(os.environ.get("SNAILTRAIL_LAB_ITERATIONS", "3000")))
    run.add_argument("--concurrency", type=int, default=4)
    run.add_argument("--seed", type=int, help="replay an earlier run exactly (every run logs its seed)")

    both = commands.add_parser("all", help="wait for MySQL, seed, then run the workload")
    both.add_argument("--scale", choices=sorted(SCALES), default=os.environ.get("SNAILTRAIL_LAB_SCALE", "small"))
    both.add_argument("--iterations", type=int, default=int(os.environ.get("SNAILTRAIL_LAB_ITERATIONS", "3000")))
    both.add_argument("--concurrency", type=int, default=4)
    both.add_argument("--seed", type=int, help="replay an earlier run exactly (every run logs its seed)")

    improve = commands.add_parser("improve", help="apply the suggested indexes, then start a new slow log")
    improve.add_argument("--dry-run", action="store_true", help="only print the statements")

    commands.add_parser("rotate", help="archive the slow log and start a new one")

    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    connect = connector(args.dsn)
    wait_for(connect)
    database = urlparse(args.dsn).path.lstrip("/") or "shop"

    if args.command in {"seed", "all"}:
        connection = connect()
        try:
            counts = Seeder(connection, SCALES[args.scale]).seed()
        finally:
            connection.close()
        log.info("seeded %d rows", sum(counts.values()))

    if args.command in {"run", "all"}:
        workload_seed = args.seed if args.seed is not None else random.SystemRandom().randrange(1, 1_000_000)
        log.info("workload seed %d", workload_seed)
        stats = Workload(connect, SCALES[args.scale], seed=workload_seed).run(args.iterations, args.concurrency)
        log.info("ran %d statements in %.1fs (%d errors)", stats.total, stats.seconds, sum(stats.errors.values()))
        for name, count in stats.executed.most_common():
            log.info("  %-20s %6d", name, count)

    if args.command in {"improve", "rotate"}:
        connection = connect()
        try:
            improver = Improver(connection, args.slow_log, database)
            if args.command == "improve":
                statements = improver.plan()
                for statement in statements:
                    print(statement)
                if args.dry_run:
                    return 0
                improver.apply(statements)
            improver.rotate()
        finally:
            connection.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
