from __future__ import annotations

from pathlib import Path

PACKAGE_DIR: Path = Path(__file__).resolve().parent
WEB_DIR: Path = PACKAGE_DIR.parent  # web/

DATA_DIR: Path = WEB_DIR / "data"
STATIC_DIR: Path = WEB_DIR / "static"
TEMPLATES_DIR: Path = WEB_DIR / "templates"

RESULTS_FILE: Path = DATA_DIR / "results.json"
INDEX_FILE: Path = TEMPLATES_DIR / "index.html"
