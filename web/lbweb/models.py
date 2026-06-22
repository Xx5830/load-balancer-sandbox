from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from .config import ALGORITHMS

_STICKY = ("none", "client", "group")
_GEN_TYPES = ("constant", "uniform", "normal", "exponential", "lognormal", "sequence")


class PresetError(ValueError):
    pass


def _req(d: dict[str, Any], key: str, where: str) -> Any:
    if key not in d:
        raise PresetError(f"{where}: отсутствует поле '{key}'")
    return d[key]


def _check_generator(g: Any, where: str) -> None:
    if isinstance(g, list):
        if not all(isinstance(x, int | float) for x in g):
            raise PresetError(f"{where}: массив генератора должен содержать числа")
        return
    if not isinstance(g, dict):
        raise PresetError(f"{where}: генератор должен быть объектом или массивом чисел")
    t = _req(g, "type", where)
    if t not in _GEN_TYPES:
        raise PresetError(f"{where}: неизвестный тип генератора '{t}'")
    need = {
        "constant": ("value",),
        "uniform": ("min", "max"),
        "normal": ("center", "deviation"),
        "exponential": ("center",),
        "lognormal": ("center", "deviation"),
        "sequence": ("values",),
    }[t]
    for k in need:
        _req(g, k, f"{where}.{t}")


def _check_background_load(value: Any, where: str) -> None:
    if isinstance(value, int | float):
        if not 0 <= float(value) <= 1:
            raise PresetError(f"{where}: число background_load должно быть в диапазоне [0, 1]")
        return
    _check_generator(value, where)


@dataclass(slots=True)
class ServerConfig:
    weight: int
    capacity: float = 1.0
    background_load: Any = field(default_factory=lambda: {"type": "constant", "value": 0.0})
    max_parallel_requests: int = 1
    start_at_ms: int = 0
    crash_at_ms: int | None = None

    @classmethod
    def from_dict(cls, d: dict[str, Any], where: str) -> ServerConfig:
        weight = int(_req(d, "weight", where))
        if weight <= 0:
            raise PresetError(f"{where}: weight должен быть > 0")
        background_load = d.get("background_load", {"type": "constant", "value": 0.0})
        _check_background_load(background_load, f"{where}.background_load")
        return cls(
            weight=weight,
            capacity=float(d.get("capacity", 1.0)),
            background_load=background_load,
            max_parallel_requests=int(d.get("max_parallel_requests", 1)),
            start_at_ms=int(d.get("start_at_ms", 0)),
            crash_at_ms=None if d.get("crash_at_ms") is None else int(d["crash_at_ms"]),
        )

    def to_dict(self) -> dict[str, Any]:
        out: dict[str, Any] = {
            "weight": self.weight,
            "capacity": self.capacity,
            "background_load": self.background_load,
            "max_parallel_requests": self.max_parallel_requests,
            "start_at_ms": self.start_at_ms,
        }
        if self.crash_at_ms is not None:
            out["crash_at_ms"] = self.crash_at_ms
        return out


@dataclass(slots=True)
class ClientGroupConfig:
    count: int
    inter_arrival_ms: Any
    task_cost: Any
    sticky_scope: str = "none"
    max_requests: int = 0

    @classmethod
    def from_dict(cls, d: dict[str, Any], where: str) -> ClientGroupConfig:
        count = int(_req(d, "count", where))
        if count <= 0:
            raise PresetError(f"{where}: count должен быть > 0")
        scope = str(d.get("sticky_scope", "none"))
        if scope not in _STICKY:
            raise PresetError(f"{where}: sticky_scope должен быть one of {_STICKY}")
        inter = _req(d, "inter_arrival_ms", where)
        cost = _req(d, "task_cost", where)
        _check_generator(inter, f"{where}.inter_arrival_ms")
        _check_generator(cost, f"{where}.task_cost")
        return cls(
            count=count,
            inter_arrival_ms=inter,
            task_cost=cost,
            sticky_scope=scope,
            max_requests=int(d.get("max_requests", 0)),
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "count": self.count,
            "sticky_scope": self.sticky_scope,
            "inter_arrival_ms": self.inter_arrival_ms,
            "task_cost": self.task_cost,
            "max_requests": self.max_requests,
        }


@dataclass(slots=True)
class Preset:
    name: str
    servers: list[ServerConfig]
    client_groups: list[ClientGroupConfig]
    description: str = ""
    seed: int = 42
    duration_ms: int = 30000
    warmup_ms: int = 0
    total_request_limit: int = 0
    manager_workers: int = 0

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Preset:
        name = str(_req(d, "name", "preset")).strip()
        if not name:
            raise PresetError("preset: name не может быть пустым")
        servers = _req(d, "servers", "preset")
        groups = _req(d, "client_groups", "preset")
        if not isinstance(servers, list) or not servers:
            raise PresetError("preset: нужен непустой список servers")
        if not isinstance(groups, list) or not groups:
            raise PresetError("preset: нужен непустой список client_groups")
        return cls(
            name=name,
            servers=[ServerConfig.from_dict(s, f"servers[{i}]") for i, s in enumerate(servers)],
            client_groups=[
                ClientGroupConfig.from_dict(g, f"client_groups[{i}]")
                for i, g in enumerate(groups)
            ],
            description=str(d.get("description", "")),
            seed=int(d.get("seed", 42)),
            duration_ms=int(d.get("duration_ms", 30000)),
            warmup_ms=int(d.get("warmup_ms", 0)),
            total_request_limit=int(d.get("total_request_limit", 0)),
            manager_workers=int(d.get("manager_workers", 0)),
        )

    def to_dict(self, algorithm: str | None = None) -> dict[str, Any]:
        out: dict[str, Any] = {
            "name": self.name,
            "description": self.description,
            "seed": self.seed,
            "duration_ms": self.duration_ms,
            "warmup_ms": self.warmup_ms,
            "total_request_limit": self.total_request_limit,
            "manager_workers": self.manager_workers,
            "servers": [s.to_dict() for s in self.servers],
            "client_groups": [g.to_dict() for g in self.client_groups],
        }
        if algorithm is not None:
            out["algorithm"] = algorithm
        return out


@dataclass(slots=True)
class RunResult:
    algorithm: str
    result: dict[str, Any]
    error: str = ""

    def to_dict(self) -> dict[str, Any]:
        return {"algorithm": self.algorithm, "result": self.result, "error": self.error}


@dataclass(slots=True)
class RunBatch:
    preset_name: str
    runs: list[RunResult] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {"preset_name": self.preset_name, "runs": [r.to_dict() for r in self.runs]}


def validate_algorithms(algos: list[str]) -> list[str]:
    if not algos:
        return list(ALGORITHMS)
    bad = [a for a in algos if a not in ALGORITHMS]
    if bad:
        raise PresetError(f"неизвестные алгоритмы: {bad}")
    return algos
