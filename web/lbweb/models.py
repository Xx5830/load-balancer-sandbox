from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any, Self


@dataclass(slots=True, frozen=True)
class ServerConfig:
    id: int
    weight: int
    capacity: float  # «свободность» 0..1

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Self:
        return cls(id=int(d["id"]), weight=int(d["weight"]), capacity=float(d["capacity"]))


@dataclass(slots=True, frozen=True)
class ServerStats:
    id: int
    weight: int
    capacity: float
    requests: int
    total_time_ms: int
    avg_time_ms: float
    share_pct: float

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Self:
        return cls(
            id=int(d["id"]),
            weight=int(d["weight"]),
            capacity=float(d["capacity"]),
            requests=int(d["requests"]),
            total_time_ms=int(d["total_time_ms"]),
            avg_time_ms=float(d["avg_time_ms"]),
            share_pct=float(d["share_pct"]),
        )


@dataclass(slots=True, frozen=True)
class TimelinePoint:
    t_ms: int
    latency_ms: float
    active_connections: int

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Self:
        return cls(
            t_ms=int(d["t_ms"]),
            latency_ms=float(d["latency_ms"]),
            active_connections=int(d["active_connections"]),
        )


@dataclass(slots=True, frozen=True)
class PolicySummary:
    avg_latency_ms: float
    peak_latency_ms: float
    p95_latency_ms: float
    total_tasks: int
    throughput_rps: float

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Self:
        return cls(
            avg_latency_ms=float(d["avg_latency_ms"]),
            peak_latency_ms=float(d["peak_latency_ms"]),
            p95_latency_ms=float(d["p95_latency_ms"]),
            total_tasks=int(d["total_tasks"]),
            throughput_rps=float(d["throughput_rps"]),
        )


@dataclass(slots=True, frozen=True)
class PolicyResult:
    name: str
    summary: PolicySummary
    servers: list[ServerStats]
    timeline: list[TimelinePoint]

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Self:
        return cls(
            name=str(d["name"]),
            summary=PolicySummary.from_dict(d["summary"]),
            servers=[ServerStats.from_dict(s) for s in d["servers"]],
            timeline=[TimelinePoint.from_dict(t) for t in d["timeline"]],
        )


@dataclass(slots=True, frozen=True)
class Meta:
    generated_at: str  # ISO-8601
    source: str  # "demo" | "benchmark"
    duration_ms: int
    total_tasks: int
    servers: list[ServerConfig]

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Self:
        return cls(
            generated_at=str(d["generated_at"]),
            source=str(d["source"]),
            duration_ms=int(d["duration_ms"]),
            total_tasks=int(d["total_tasks"]),
            servers=[ServerConfig.from_dict(s) for s in d["servers"]],
        )


@dataclass(slots=True, frozen=True)
class Results:
    meta: Meta
    policies: list[PolicyResult] = field(default_factory=list)

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> Self:
        return cls(
            meta=Meta.from_dict(d["meta"]),
            policies=[PolicyResult.from_dict(p) for p in d["policies"]],
        )

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)

    def policy_names(self) -> list[str]:
        return [p.name for p in self.policies]
