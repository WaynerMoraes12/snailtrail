from __future__ import annotations

import os
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import unquote, urlparse


@dataclass(frozen=True)
class MySQLDsn:
    host: str
    port: int
    user: str
    password: str
    database: str

    @classmethod
    def parse(cls, dsn: str) -> MySQLDsn:
        url = urlparse(dsn)
        if url.scheme not in {"mysql", "mysql+pymysql"}:
            raise ValueError(f"expected a mysql:// DSN, got {dsn!r}")
        return cls(
            host=url.hostname or "localhost",
            port=url.port or 3306,
            user=unquote(url.username or "root"),
            password=unquote(url.password or ""),
            database=url.path.lstrip("/"),
        )

    def with_database(self, database: str) -> MySQLDsn:
        return MySQLDsn(self.host, self.port, self.user, self.password, database)

    def redacted(self) -> str:
        return f"mysql://{self.user}:***@{self.host}:{self.port}/{self.database}"


def _flag(value: str | None, default: bool) -> bool:
    if value is None or value == "":
        return default
    return value.strip().lower() in {"1", "true", "yes", "on"}


def _number(env: Mapping[str, str], name: str, default: int) -> int:
    value = env.get(name)
    if value is None or value == "":
        return default
    try:
        return int(value)
    except ValueError as error:
        raise ValueError(f"{name} must be an integer, got {value!r}") from error


@dataclass(frozen=True)
class Settings:
    slow_log: Path
    history_dsn: str
    target: MySQLDsn | None = None
    database_filter: str | None = None
    top: int = 50
    threads: int = 0
    explain: bool = True
    explain_top: int = 15
    analyze_on_start: bool = True
    interval_seconds: int = 0
    regression_threshold: float = 1.5
    host: str = "0.0.0.0"
    port: int = 8080

    @property
    def in_memory(self) -> bool:
        return self.history_dsn.startswith("memory:")

    @property
    def history(self) -> MySQLDsn:
        return MySQLDsn.parse(self.history_dsn)

    @classmethod
    def from_env(cls, env: Mapping[str, str] | None = None) -> Settings:
        env = os.environ if env is None else env
        target = env.get("SNAILTRAIL_TARGET_DSN")
        return cls(
            slow_log=Path(env.get("SNAILTRAIL_SLOW_LOG", "/var/log/mysql/slow.log")),
            history_dsn=env.get("SNAILTRAIL_HISTORY_DSN", "memory://"),
            target=MySQLDsn.parse(target) if target else None,
            database_filter=env.get("SNAILTRAIL_DATABASE") or None,
            top=_number(env, "SNAILTRAIL_TOP", 50),
            threads=_number(env, "SNAILTRAIL_THREADS", 0),
            explain=_flag(env.get("SNAILTRAIL_EXPLAIN"), True),
            explain_top=_number(env, "SNAILTRAIL_EXPLAIN_TOP", 15),
            analyze_on_start=_flag(env.get("SNAILTRAIL_ANALYZE_ON_START"), True),
            interval_seconds=_number(env, "SNAILTRAIL_INTERVAL", 0),
            regression_threshold=float(env.get("SNAILTRAIL_REGRESSION_THRESHOLD", "1.5")),
            host=env.get("SNAILTRAIL_HOST", "0.0.0.0"),
            port=_number(env, "SNAILTRAIL_PORT", 8080),
        )
