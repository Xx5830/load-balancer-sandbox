from __future__ import annotations

from pathlib import Path

PACKAGE_DIR: Path = Path(__file__).resolve().parent
WEB_DIR: Path = PACKAGE_DIR.parent  # web/
PROJECT_DIR: Path = WEB_DIR.parent  # репозиторий

DATA_DIR: Path = WEB_DIR / "data"
STATIC_DIR: Path = WEB_DIR / "static"
TEMPLATES_DIR: Path = WEB_DIR / "templates"
BUILD_DIR: Path = PROJECT_DIR / "build"

RUNS_DIR: Path = DATA_DIR / "runs"
PRESETS_DIR: Path = DATA_DIR / "presets"
INDEX_FILE: Path = TEMPLATES_DIR / "index.html"

ALGORITHMS: tuple[str, ...] = (
    "RoundRobin",
    "WeightRoundRobin",
    "LeastConnections",
    "ConsistentHashing",
)
