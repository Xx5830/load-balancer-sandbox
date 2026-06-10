from __future__ import annotations

import argparse
import math
import random
from datetime import UTC, datetime
from pathlib import Path

from .config import RESULTS_FILE
from .data import save_results
from .models import (
    Meta,
    PolicyResult,
    PolicySummary,
    Results,
    ServerConfig,
    ServerStats,
    TimelinePoint,
)

POLICIES: tuple[str, ...] = (
    "RoundRobin",
    "WeightRoundRobin",
    "LeastConnections",
    "ConsistentHashing",
)

TIMELINE_POINTS = 60


def _make_servers(count: int, rng: random.Random) -> list[ServerConfig]:
    return [
        ServerConfig(
            id=i,
            weight=rng.choice((1, 1, 2, 2, 3, 4)),
            capacity=round(rng.uniform(0.4, 1.0), 2),
        )
        for i in range(count)
    ]


def _distribute_requests(
    policy: str,
    servers: list[ServerConfig],
    total_tasks: int,
    rng: random.Random,
) -> list[int]:
    n = len(servers)
    if policy == "RoundRobin":
        counts = [total_tasks // n] * n
        for i in range(total_tasks - sum(counts)):
            counts[i % n] += 1
    elif policy == "WeightRoundRobin":
        total_w = sum(s.weight for s in servers)
        counts = [round(total_tasks * s.weight / total_w) for s in servers]
    elif policy == "LeastConnections":
        power = [s.weight * s.capacity for s in servers]  # вес * capacity
        total_p = sum(power)
        counts = [round(total_tasks * p / total_p) for p in power]
    else:  # ConsistentHashing: почти равномерно, с разбросом
        counts = [total_tasks // n] * n
        for i in range(total_tasks - sum(counts)):
            counts[i % n] += 1
        counts = [c + rng.randint(-(c // 5) or -1, (c // 5) or 1) for c in counts]

    counts = [max(0, c) for c in counts]  # подогнать сумму под total_tasks
    counts[0] += total_tasks - sum(counts)
    counts[0] = max(0, counts[0])
    return counts


def _build_server_stats(
    cfg: ServerConfig, requests: int, total_tasks: int, rng: random.Random
) -> ServerStats:
    # меньше capacity → дольше обработка
    base_task_ms = rng.uniform(40, 90) / max(cfg.capacity, 0.1)
    total_time = int(requests * base_task_ms * rng.uniform(0.9, 1.1))
    avg_time = total_time / requests if requests else 0.0
    share = requests / total_tasks * 100 if total_tasks else 0.0
    return ServerStats(
        id=cfg.id,
        weight=cfg.weight,
        capacity=cfg.capacity,
        requests=requests,
        total_time_ms=total_time,
        avg_time_ms=round(avg_time, 2),
        share_pct=round(share, 2),
    )


def _build_timeline(
    avg_latency: float, duration_ms: int, rng: random.Random
) -> list[TimelinePoint]:
    step = duration_ms // TIMELINE_POINTS
    timeline: list[TimelinePoint] = []
    for i in range(TIMELINE_POINTS):
        phase = i / TIMELINE_POINTS
        # волнообразная нагрузка: разогрев и спад
        warmup = min(1.0, phase * 4)
        cooldown = min(1.0, (1 - phase) * 4)
        load = warmup * cooldown
        wave = 1 + 0.25 * math.sin(phase * math.pi * 6)

        latency = avg_latency * wave * (0.8 + 0.4 * load) * rng.uniform(0.92, 1.08)
        active = int((4 + 18 * load) * wave * rng.uniform(0.85, 1.15))
        timeline.append(
            TimelinePoint(
                t_ms=i * step,
                latency_ms=round(max(0.0, latency), 2),
                active_connections=max(0, active),
            )
        )
    return timeline


def _build_policy(
    policy: str,
    servers: list[ServerConfig],
    total_tasks: int,
    duration_ms: int,
    rng: random.Random,
) -> PolicyResult:
    counts = _distribute_requests(policy, servers, total_tasks, rng)
    stats = [
        _build_server_stats(cfg, req, total_tasks, rng)
        for cfg, req in zip(servers, counts, strict=True)
    ]

    avg_latency = (
        sum(s.avg_time_ms * s.requests for s in stats) / total_tasks
        if total_tasks
        else 0.0
    )
    peak_latency = max((s.avg_time_ms for s in stats), default=0.0) * rng.uniform(1.6, 2.4)
    p95 = avg_latency * rng.uniform(1.3, 1.7)
    throughput = total_tasks / (duration_ms / 1000) if duration_ms else 0.0

    return PolicyResult(
        name=policy,
        summary=PolicySummary(
            avg_latency_ms=round(avg_latency, 2),
            peak_latency_ms=round(peak_latency, 2),
            p95_latency_ms=round(p95, 2),
            total_tasks=total_tasks,
            throughput_rps=round(throughput, 2),
        ),
        servers=stats,
        timeline=_build_timeline(avg_latency, duration_ms, rng),
    )


def generate(
    *,
    servers: int = 5,
    total_tasks: int = 10_000,
    duration_ms: int = 30_000,
    seed: int | None = 42,
) -> Results:
    rng = random.Random(seed)
    servers_cfg = _make_servers(servers, rng)
    policies = [
        _build_policy(name, servers_cfg, total_tasks, duration_ms, rng)
        for name in POLICIES
    ]
    return Results(
        meta=Meta(
            generated_at=datetime.now(UTC).isoformat(),
            source="demo",
            duration_ms=duration_ms,
            total_tasks=total_tasks,
            servers=servers_cfg,
        ),
        policies=policies,
    )


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Генератор демо-данных балансировки")
    parser.add_argument("--servers", type=int, default=5)
    parser.add_argument("--tasks", type=int, default=10_000)
    parser.add_argument("--duration-ms", type=int, default=30_000)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--out", type=Path, default=RESULTS_FILE)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> None:
    args = _parse_args(argv)
    results = generate(
        servers=args.servers,
        total_tasks=args.tasks,
        duration_ms=args.duration_ms,
        seed=args.seed,
    )
    save_results(results, args.out)
    print(
        f"Записано: {args.out}  ({len(results.policies)} политик, "
        f"{args.servers} серверов, {args.tasks} тасок)"
    )


if __name__ == "__main__":
    main()
