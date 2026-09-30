from __future__ import annotations

import logging

from .config import Settings


def main() -> None:
    import uvicorn

    from .app import create_app

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    settings = Settings.from_env()
    logging.getLogger("snailtrail.dashboard").info(
        "slow log %s, history %s", settings.slow_log, "in memory" if settings.in_memory else settings.history.redacted()
    )
    uvicorn.run(create_app(settings), host=settings.host, port=settings.port, log_level="info")


if __name__ == "__main__":
    main()
