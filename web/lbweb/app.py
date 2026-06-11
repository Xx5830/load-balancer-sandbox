from __future__ import annotations

from importlib.metadata import version
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles

from .config import ALGORITHMS, INDEX_FILE, STATIC_DIR
from .data import (
    delete_batch,
    list_batches,
    list_presets,
    load_batch,
    save_batch,
    save_preset,
)
from .models import Preset, PresetError, validate_algorithms
from .runner import BinaryNotFoundError, find_binary, run_batch

app = FastAPI(title="Load Balancing Lab", version=version("lbweb"))
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


@app.get("/", response_class=HTMLResponse)
def index() -> HTMLResponse:
    return HTMLResponse(INDEX_FILE.read_text(encoding="utf-8"))


@app.get("/api/meta")
def api_meta() -> dict[str, Any]:
    return {"algorithms": list(ALGORITHMS), "binary": str(find_binary() or "")}


@app.post("/api/run")
def api_run(payload: dict[str, Any]) -> dict[str, Any]:
    try:
        preset = Preset.from_dict(payload.get("preset", {}))
        algorithms = validate_algorithms(payload.get("algorithms", []))
    except PresetError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    try:
        batch = run_batch(preset, algorithms)
    except BinaryNotFoundError as exc:
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    run_id = save_batch(batch)
    return {"id": run_id, **batch.to_dict()}


@app.post("/api/preset")
def api_save_preset(payload: dict[str, Any]) -> dict[str, str]:
    try:
        preset = Preset.from_dict(payload)
    except PresetError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    return {"name": save_preset(preset)}


@app.get("/api/presets")
def api_presets() -> list[dict[str, Any]]:
    return list_presets()


@app.get("/api/runs")
def api_runs() -> list[dict[str, Any]]:
    return list_batches()


@app.get("/api/runs/{run_id}")
def api_run_get(run_id: str) -> dict[str, Any]:
    try:
        return load_batch(run_id)
    except FileNotFoundError as exc:
        raise HTTPException(status_code=404, detail="прогон не найден") from exc


@app.delete("/api/runs/{run_id}")
def api_run_delete(run_id: str) -> dict[str, bool]:
    delete_batch(run_id)
    return {"ok": True}


def main() -> None:
    import uvicorn

    uvicorn.run("lbweb.app:app", host="127.0.0.1", port=8000, reload=True)


if __name__ == "__main__":
    main()
