from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from .config import BUILD_DIR
from .models import Preset, RunBatch, RunResult, validate_algorithms

RUN_TIMEOUT_S = 120


class BinaryNotFoundError(RuntimeError):
    pass


def find_binary() -> Path | None:
    for pat in ("*_main", "*/main"):
        for p in sorted(BUILD_DIR.glob(pat)):
            if p.is_file() and p.stat().st_mode & 0o111:
                return p
    return None


def _run_one(binary: Path, preset: Preset, algorithm: str) -> RunResult:
    with tempfile.TemporaryDirectory() as tmp:
        in_path = Path(tmp) / "preset.json"
        out_path = Path(tmp) / "result.json"
        in_path.write_text(
            json.dumps(preset.to_dict(algorithm), ensure_ascii=False), encoding="utf-8"
        )
        try:
            proc = subprocess.run(
                [str(binary), str(in_path), str(out_path)],
                capture_output=True,
                text=True,
                timeout=RUN_TIMEOUT_S,
            )
        except subprocess.TimeoutExpired:
            return RunResult(algorithm, {}, f"таймаут {RUN_TIMEOUT_S}s")
        if proc.returncode != 0:
            return RunResult(algorithm, {}, proc.stderr.strip() or "ненулевой код возврата")
        try:
            result: dict[str, Any] = json.loads(out_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            return RunResult(algorithm, {}, f"не удалось прочитать результат: {exc}")
        return RunResult(algorithm, result)


def run_batch(preset: Preset, algorithms: list[str]) -> RunBatch:
    binary = find_binary()
    if binary is None:
        raise BinaryNotFoundError(
            "C++ бинарь не найден в build/. Соберите: just build [debug|release]"
        )
    algos = validate_algorithms(algorithms)
    return RunBatch(
        preset_name=preset.name,
        runs=[_run_one(binary, preset, a) for a in algos],
    )
