import os
from pathlib import Path

import pytest

import snailtrail

ROOT = Path(__file__).resolve().parents[2]
SAMPLES = Path(os.environ.get("SNAILTRAIL_SAMPLES_DIR", ROOT / "samples"))


@pytest.fixture(scope="session")
def samples() -> Path:
    return SAMPLES


@pytest.fixture(scope="session")
def shop_schema() -> snailtrail.SchemaCatalog:
    return snailtrail.SchemaCatalog.from_ddl((SAMPLES / "shop_schema.sql").read_text(encoding="utf-8"))


@pytest.fixture(scope="session")
def generated_log() -> str:
    return snailtrail.generate_log(events=4000, seed=21)


@pytest.fixture(scope="session")
def report(generated_log: str, shop_schema: snailtrail.SchemaCatalog) -> snailtrail.Report:
    return snailtrail.analyze_text(generated_log, schema=shop_schema, threads=2)
