from collections.abc import Sequence
from typing import Literal, TypedDict

__version__: str

Severity = Literal["info", "warning", "critical"]
SortKey = Literal["time", "calls", "avg", "p95", "max", "rows"]
ReportFormat = Literal["text", "json", "markdown", "md"]

class ParseError(ValueError):
    offset: int

class Finding:
    rule_id: str
    rule_name: str
    severity: Severity
    title: str
    detail: str
    suggestion: str
    def to_dict(self) -> dict[str, str]: ...

class Fingerprint:
    id: str
    kind: str
    text: str

class Sample(TypedDict):
    sql: str
    database: str
    user: str
    host: str
    query_time_us: int
    rows_examined: int
    rows_sent: int
    timestamp: int

class Flags(TypedDict):
    full_scan: float
    filesort: float
    tmp_table: float
    tmp_table_on_disk: float

class Totals(TypedDict):
    events: int
    query_time_us: int
    lock_time_us: int
    rows_sent: int
    rows_examined: int
    rows_affected: int
    first_seen: int
    last_seen: int

class RuleDescription(TypedDict):
    id: str
    name: str
    summary: str
    severity: Severity
    needs_schema: bool
    needs_stats: bool

class SchemaCatalog:
    tables: list[str]
    warnings: list[str]
    @staticmethod
    def from_ddl(ddl: str) -> SchemaCatalog: ...
    def has_table(self, name: str) -> bool: ...
    def indexes(self, table: str) -> list[tuple[str, list[str]]]: ...
    def __len__(self) -> int: ...

class QueryClass:
    rank: int
    id: str
    kind: str
    fingerprint: str
    label: str
    tables: list[str]
    time_share: float
    findings: list[Finding]
    calls: int
    total_time_us: int
    avg_time_us: float
    min_time_us: int
    max_time_us: int
    p50_us: int
    p95_us: int
    p99_us: int
    lock_time_us: int
    rows_sent: int
    rows_examined: int
    rows_affected: int
    first_seen: int
    last_seen: int
    latency_decades: list[int]
    databases: list[tuple[str, int]]
    flags: Flags
    sample: Sample

class Report:
    source: str
    bytes: int
    events: int
    skipped: int
    filtered: int
    threads: int
    chunks: int
    parse_seconds: float
    elapsed_seconds: float
    schema_tables: int
    class_count: int
    classes: list[QueryClass]
    totals: Totals
    findings_by_severity: dict[Severity, int]
    def render(self, format: ReportFormat = "text", color: bool = False, width: int = 100, details: int = 10) -> str: ...

def fingerprint(sql: str) -> Fingerprint: ...
def parse_tree(sql: str) -> str: ...
def rules() -> list[RuleDescription]: ...
def advise(sql: str, schema: SchemaCatalog | None = None, disable: Sequence[str] = ()) -> list[Finding]: ...
def analyze_file(
    path: str,
    *,
    threads: int = 0,
    top: int = 0,
    sort: SortKey = "time",
    schema: SchemaCatalog | None = None,
    database: str | None = None,
    min_severity: Severity = "info",
    advise: bool = True,
    disable: Sequence[str] = (),
) -> Report: ...
def analyze_text(
    text: str,
    *,
    threads: int = 0,
    top: int = 0,
    sort: SortKey = "time",
    schema: SchemaCatalog | None = None,
    database: str | None = None,
    min_severity: Severity = "info",
    advise: bool = True,
    disable: Sequence[str] = (),
) -> Report: ...
def generate_log(events: int = 10000, seed: int = 42, extra_fields: bool = True) -> str: ...
