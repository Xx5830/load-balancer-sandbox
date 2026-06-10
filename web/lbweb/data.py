from __future__ import annotations

import json
from pathlib import Path

from .config import RESULTS_FILE
from .models import Results


class ResultsNotFoundError(FileNotFoundError):
    pass


def load_results(path: Path = RESULTS_FILE) -> Results:
    if not path.exists():
        raise ResultsNotFoundError(path)
    payload = json.loads(path.read_text(encoding="utf-8"))
    return Results.from_dict(payload)


def save_results(results: Results, path: Path = RESULTS_FILE) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(results.to_dict(), indent=2, ensure_ascii=False)
    path.write_text(text, encoding="utf-8")
