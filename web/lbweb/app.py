from __future__ import annotations

from importlib.metadata import version
from typing import Any

from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles

from .config import INDEX_FILE, RESULTS_FILE, STATIC_DIR
from .data import ResultsNotFoundError, load_results
from .models import Results

app = FastAPI(title="Load Balancing Results", version=version("lbweb"))
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


def _require_results() -> Results:
    try:
        return load_results()
    except ResultsNotFoundError as exc:
        raise HTTPException(
            status_code=404,
            detail="results.json не найден. Сгенерируйте: lbweb-demo",
        ) from exc


@app.get("/", response_class=HTMLResponse)
def index() -> HTMLResponse:
    return HTMLResponse(INDEX_FILE.read_text(encoding="utf-8"))


@app.get("/api/results")
def api_results() -> dict[str, Any]:
    return _require_results().to_dict()


@app.get("/api/policies")
def api_policies() -> list[str]:
    return _require_results().policy_names()


@app.get("/api/health")
def api_health() -> dict[str, Any]:
    return {"status": "ok", "has_data": RESULTS_FILE.exists()}


def main() -> None:
    import uvicorn

    uvicorn.run("lbweb.app:app", host="127.0.0.1", port=8000, reload=True)


if __name__ == "__main__":
    main()
