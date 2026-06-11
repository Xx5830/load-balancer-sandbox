from __future__ import annotations

import json
import re
import time
from typing import Any

from .config import PRESETS_DIR, RUNS_DIR
from .models import Preset, RunBatch

_SLUG = re.compile(r"[^a-zA-Z0-9_-]+")


def _slug(name: str) -> str:
    return _SLUG.sub("-", name.strip()).strip("-").lower() or "run"


def save_batch(batch: RunBatch) -> str:
    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    run_id = f"{int(time.time())}-{_slug(batch.preset_name)}"
    (RUNS_DIR / f"{run_id}.json").write_text(
        json.dumps(batch.to_dict(), indent=2, ensure_ascii=False), encoding="utf-8"
    )
    return run_id


def list_batches() -> list[dict[str, Any]]:
    if not RUNS_DIR.exists():
        return []
    items: list[dict[str, Any]] = []
    for p in sorted(RUNS_DIR.glob("*.json"), reverse=True):
        data = json.loads(p.read_text(encoding="utf-8"))
        items.append(
            {
                "id": p.stem,
                "preset_name": data.get("preset_name", ""),
                "algorithms": [r["algorithm"] for r in data.get("runs", [])],
            }
        )
    return items


def load_batch(run_id: str) -> dict[str, Any]:
    path = RUNS_DIR / f"{_slug(run_id)}.json"
    if not path.exists():
        raise FileNotFoundError(run_id)
    data: dict[str, Any] = json.loads(path.read_text(encoding="utf-8"))
    return data


def delete_batch(run_id: str) -> None:
    (RUNS_DIR / f"{_slug(run_id)}.json").unlink(missing_ok=True)


def save_preset(preset: Preset) -> str:
    PRESETS_DIR.mkdir(parents=True, exist_ok=True)
    name = _slug(preset.name)
    (PRESETS_DIR / f"{name}.json").write_text(
        json.dumps(preset.to_dict(), indent=2, ensure_ascii=False), encoding="utf-8"
    )
    return name


def list_presets() -> list[dict[str, Any]]:
    if not PRESETS_DIR.exists():
        return []
    return [
        json.loads(p.read_text(encoding="utf-8"))
        for p in sorted(PRESETS_DIR.glob("*.json"))
    ]
