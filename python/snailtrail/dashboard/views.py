from __future__ import annotations

import math
import re
from collections.abc import Sequence
from dataclasses import dataclass
from datetime import datetime

DECADE_LABELS = ("1µs", "10µs", "100µs", "1ms", "10ms", "100ms", "1s", "10s+")

_BREAK_BEFORE = re.compile(
    r"\s+(?=\b(?:from|where|group by|order by|having|limit|left join|right join|inner join|join|"
    r"straight_join|union all|union|on duplicate key update|values|set)\b)",
    re.IGNORECASE,
)
_INDENT_BEFORE = re.compile(r"\s+(?=\b(?:and|or)\b)", re.IGNORECASE)


def duration(micros: float | int | None) -> str:
    if micros is None or micros != micros:
        return "-"
    seconds = float(micros) / 1e6
    if seconds < 1e-3:
        return f"{seconds * 1e6:.0f} µs"
    if seconds < 0.1:
        return f"{seconds * 1e3:.1f} ms"
    if seconds < 1:
        return f"{seconds * 1e3:.0f} ms"
    if seconds < 10:
        return f"{seconds:.2f} s"
    if seconds < 60:
        return f"{seconds:.1f} s"
    total = round(seconds)
    if total < 3600:
        return f"{total // 60}m {total % 60:02d}s"
    if total < 86400:
        return f"{total // 3600}h {(total % 3600) // 60:02d}m"
    return f"{total // 86400}d {(total % 86400) // 3600:02d}h"


def compact(value: float | int | None) -> str:
    if value is None:
        return "-"
    n = float(value)
    if n < 1000:
        return f"{n:.0f}" if n == int(n) else (f"{n:.1f}" if n < 10 else f"{n:.0f}")
    for suffix in ("k", "M", "B", "T"):
        n /= 1000
        if n < 1000 or suffix == "T":
            return f"{n:.1f}{suffix}" if n < 100 else f"{n:.0f}{suffix}"
    return str(value)


def count(value: int | None) -> str:
    return "-" if value is None else f"{value:,}"


def percent(ratio: float | None, digits: int = 1) -> str:
    return "-" if ratio is None else f"{ratio * 100:.{digits}f}%"


def when(moment: datetime | None) -> str:
    return "-" if moment is None else moment.strftime("%Y-%m-%d %H:%M:%S")


def data_size(size: int | None) -> str:
    if size is None:
        return "-"
    value = float(size)
    for unit in ("B", "KB", "MB", "GB"):
        if value < 1024 or unit == "GB":
            return f"{value:.0f} {unit}" if unit == "B" else f"{value:.1f} {unit}"
        value /= 1024
    return f"{value:.1f} TB"


def pretty_sql(sql: str) -> str:
    text = " ".join(sql.split())
    text = _BREAK_BEFORE.sub("\n", text)
    return _INDENT_BEFORE.sub("\n  ", text)


_SQL_START = re.compile(r"^(?:(?:ALTER|CREATE|DROP|SELECT|UPDATE|DELETE|INSERT|WITH|NOT EXISTS)\b|\.\.\.\s)")


@dataclass(frozen=True)
class FixPart:
    kind: str
    text: str


def fix_parts(suggestion: str) -> list[FixPart]:
    parts: list[FixPart] = []

    def add(kind: str, text: str) -> None:
        if parts and parts[-1].kind == kind == "sql":
            parts[-1] = FixPart("sql", parts[-1].text + "\n" + text)
        else:
            parts.append(FixPart(kind, text))

    for raw in suggestion.splitlines():
        line = raw.strip()
        if not line:
            continue
        if _SQL_START.match(line):
            add("sql", line)
            continue
        head, sep, tail = line.partition(": ")
        if sep and _SQL_START.match(tail):
            add("text", head + ":")
            add("sql", tail)
        else:
            add("text", line)
    return parts


def ratio_label(ratio: float) -> str:
    if ratio >= 1:
        return f"{ratio:.1f}× slower"
    return f"{1 / ratio:.1f}× faster" if ratio > 0 else "-"


@dataclass(frozen=True)
class Bar:
    label: str
    value: int
    height: float


def decade_bars(decades: Sequence[int], height: float = 64.0) -> list[Bar]:
    peak = max(decades, default=0)
    return [
        Bar(label, value, (value / peak) * height if peak else 0.0)
        for label, value in zip(DECADE_LABELS, decades, strict=False)
    ]


@dataclass(frozen=True)
class Series:
    name: str
    points: str
    dots: tuple[tuple[float, float, str], ...]


@dataclass(frozen=True)
class LineChart:
    width: int
    height: int
    left: int
    series: tuple[Series, ...]
    ticks: tuple[tuple[float, str], ...]
    labels: tuple[tuple[float, str], ...]


def line_chart(
    named_values: Sequence[tuple[str, Sequence[float]]],
    labels: Sequence[str],
    width: int = 640,
    height: int = 180,
    pad: int = 64,
) -> LineChart:
    values = [v for _, vs in named_values for v in vs if v > 0]
    top = max(values, default=1.0)
    magnitude = 10 ** math.floor(math.log10(top)) if top > 0 else 1
    ceiling = math.ceil(top / magnitude) * magnitude
    count_points = max((len(vs) for _, vs in named_values), default=1)

    top_pad, bottom_pad, right_pad = 12, 32, 16

    def x(i: int) -> float:
        return pad + (width - pad - right_pad) * (i / (count_points - 1) if count_points > 1 else 0.5)

    def y(v: float) -> float:
        return height - bottom_pad - (height - top_pad - bottom_pad) * (v / ceiling if ceiling else 0)

    series = []
    for name, vs in named_values:
        coords = [(x(i), y(v), duration(v)) for i, v in enumerate(vs)]
        series.append(Series(name, " ".join(f"{cx:.1f},{cy:.1f}" for cx, cy, _ in coords), tuple(coords)))
    ticks = tuple((y(ceiling * f), duration(ceiling * f)) for f in (0, 0.5, 1))
    step = max(1, len(labels) // 8)
    shown = tuple((x(i), label) for i, label in enumerate(labels) if i % step == 0 or i == len(labels) - 1)
    return LineChart(width, height, pad, tuple(series), ticks, shown)
