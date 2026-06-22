from __future__ import annotations

import json
import math
import random
from typing import Any

from .config import ALGORITHMS, PRESETS_DIR, RUNS_DIR
from .models import ClientGroupConfig, Preset, RunBatch, RunResult, ServerConfig

EXAMPLE = Preset(
    name="example",
    description="Пример пресета: 4 сервера, 2 группы клиентов",
    seed=42,
    duration_ms=30000,
    servers=[
        ServerConfig(weight=1, capacity=1.0, max_parallel_requests=4),
        ServerConfig(weight=2, capacity=0.8, max_parallel_requests=4),
        ServerConfig(weight=2, capacity=0.6, max_parallel_requests=2),
        ServerConfig(weight=4, capacity=1.0, max_parallel_requests=8, crash_at_ms=20000),
    ],
    client_groups=[
        ClientGroupConfig(
            count=6,
            inter_arrival_ms={"type": "exponential", "center": 80},
            task_cost={"type": "normal", "center": 0.08, "deviation": 0.02, "min": 0.01},
        ),
        ClientGroupConfig(
            count=3,
            sticky_scope="client",
            inter_arrival_ms={"type": "uniform", "min": 60, "max": 140},
            task_cost={"type": "constant", "value": 0.12},
        ),
    ],
)

_ALGO_BIAS = {
    "RoundRobin": (1.00, 1.00),
    "WeightRoundRobin": (0.92, 0.96),
    "LeastConnections": (0.85, 0.90),
    "ConsistentHashing": (1.05, 1.08),
}


def _gen_mean(g: Any) -> float:
    if isinstance(g, list):
        return sum(g) / len(g) if g else 0.0
    t = g.get("type")
    if t == "constant":
        return float(g["value"])
    if t == "uniform":
        return (float(g["min"]) + float(g["max"])) / 2
    if t in ("normal", "exponential", "lognormal"):
        return float(g["center"])
    if t == "sequence":
        vals = g.get("values") or [0]
        return float(sum(vals)) / len(vals)
    return 0.0


def _sample_result(preset: Preset, algorithm: str) -> dict[str, Any]:
    rnd = random.Random(f"{preset.seed}:{algorithm}:{preset.name}")
    lat_bias, fail_bias = _ALGO_BIAS.get(algorithm, (1.0, 1.0))

    duration = preset.duration_ms or 30000
    secs = max(1, duration // 1000)
    total_weight = sum(s.weight for s in preset.servers) or 1
    groups_n = len(preset.client_groups)
    cost_mean = sum(_gen_mean(g.task_cost) for g in preset.client_groups) / groups_n
    lat_mean = max(1.0, cost_mean * lat_bias * rnd.uniform(0.9, 1.1))
    lat_stddev = lat_mean * rnd.uniform(0.3, 0.6)

    arrival = sum(g.count / (_gen_mean(g.inter_arrival_ms) or 1.0) for g in preset.client_groups)
    requests_sent = int(arrival * duration * rnd.uniform(0.95, 1.05)) or len(preset.servers) * secs
    has_crash = any(s.crash_at_ms for s in preset.servers)
    fail_rate = min(0.25, 0.02 * fail_bias + (0.04 if has_crash else 0))
    failed_final = int(requests_sent * fail_rate)
    successful = requests_sent - failed_final
    crashed = int(failed_final * 0.5) if has_crash else 0
    overloaded = int((failed_final - crashed) * 0.6)
    timeout = failed_final - crashed - overloaded

    p50, p95, p99 = lat_mean, lat_mean + 1.64 * lat_stddev, lat_mean + 2.33 * lat_stddev
    lat_min = max(0.1, lat_mean - lat_stddev)
    lat_max = p99 * rnd.uniform(1.1, 1.4)
    bucket = max(1.0, round(lat_max / 20, 1))
    nbuckets = int(lat_max / bucket) + 1
    buckets = []
    for i in range(nbuckets):
        z = ((i + 0.5) * bucket - lat_mean) / (lat_stddev or 1)
        buckets.append(int(successful * math.exp(-0.5 * z * z) / (nbuckets * 0.4) + 0.5))

    timeline = []
    for s in range(secs):
        load = min(1.0, arrival * 1000 / (total_weight * 50) * rnd.uniform(0.8, 1.0))
        timeline.append(
            {
                "t_ms": s * 1000,
                "active_connections": int(arrival * rnd.uniform(0.5, 1.5)),
                "requests_in_flight": int(arrival * rnd.uniform(0.3, 0.8)),
                "new_requests": int(requests_sent / secs * rnd.uniform(0.8, 1.2)),
                "successful": int(successful / secs * rnd.uniform(0.8, 1.2)),
                "failed_this_second": int(failed_final / secs * rnd.uniform(0, 2)),
                "avg_latency_ms": round(lat_mean * rnd.uniform(0.8, 1.2), 2),
                "p95_latency_ms": round(p95 * rnd.uniform(0.85, 1.15), 2),
                "total_load_avg": round(load * (s + 1) / secs, 3),
            }
        )

    servers = []
    for i, sc in enumerate(preset.servers):
        share = sc.weight / total_weight
        if algorithm == "LeastConnections":
            share = (share + 1 / len(preset.servers)) / 2
        received = int(requests_sent * share)
        s_failed = int(received * fail_rate * rnd.uniform(0.5, 1.5))
        avg_t = lat_mean * rnd.uniform(0.9, 1.1)
        servers.append(
            {
                "id": i,
                "weight": sc.weight,
                "capacity": round(sc.capacity, 3),
                "max_parallel_requests": sc.max_parallel_requests,
                "start_at_ms": sc.start_at_ms,
                "crash_at_ms": sc.crash_at_ms,
                "requests_received": received,
                "successful": received - s_failed,
                "failed": s_failed,
                "total_time_processing_ms": round(received * avg_t, 1),
                "avg_time_ms": round(avg_t, 2),
                "min_time_ms": round(lat_min, 2),
                "max_time_ms": round(lat_max, 2),
                "avg_load": round(min(1.0, share * 2 * rnd.uniform(0.6, 1.0)), 3),
                "peak_load": round(min(1.0, share * 2 * rnd.uniform(1.0, 1.3) + 0.2), 3),
                "crashes": 1 if sc.crash_at_ms else 0,
            }
        )

    groups = []
    for i, g in enumerate(preset.client_groups):
        share = (g.count / (_gen_mean(g.inter_arrival_ms) or 1.0)) / (arrival or 1)
        g_sent = int(requests_sent * share)
        g_failed = int(g_sent * fail_rate * rnd.uniform(0.5, 1.5))
        groups.append(
            {
                "group_index": i,
                "sticky_scope": g.sticky_scope,
                "num_clients": g.count,
                "requests_sent": g_sent,
                "successful": g_sent - g_failed,
                "failed_final": g_failed,
                "retries": int(g_failed * rnd.uniform(0.5, 1.5)),
                "latency": {
                    "mean": round(lat_mean * rnd.uniform(0.9, 1.1), 2),
                    "p95": round(p95 * rnd.uniform(0.9, 1.1), 2),
                    "max": round(lat_max * rnd.uniform(0.9, 1.1), 2),
                },
            }
        )

    return {
        "experiment": {
            "name": preset.name,
            "algorithm": algorithm,
            "profile": "uniform",
            "preset_file": f"{preset.name}.json",
        },
        "config": {
            "duration_ms": duration,
            "warmup_ms": preset.warmup_ms,
            "total_request_limit": preset.total_request_limit,
            "manager_workers": preset.manager_workers,
            "seed": preset.seed,
            "servers": [s.to_dict() for s in preset.servers],
            "client_groups": [g.to_dict() for g in preset.client_groups],
        },
        "totals": {
            "requests_sent": requests_sent,
            "successful": successful,
            "failed_final": failed_final,
            "retries": int(failed_final * rnd.uniform(0.5, 1.5)),
            "actual_duration_ms": round(duration * rnd.uniform(1.0, 1.02), 1),
            "throughput_rps": round(successful / (duration / 1000), 2),
        },
        "latency": {
            "unit": "ms",
            "count": successful,
            "min": round(lat_min, 2),
            "max": round(lat_max, 2),
            "mean": round(lat_mean, 2),
            "stddev": round(lat_stddev, 2),
            "percentiles": {"p50": round(p50, 2), "p95": round(p95, 2), "p99": round(p99, 2)},
            "histogram": {"bucket_size_ms": bucket, "buckets": buckets},
        },
        "failures": {
            "total_final_failures": failed_final,
            "by_reason": {
                "server_crashed": crashed,
                "server_overloaded": overloaded,
                "timeout": timeout,
            },
        },
        "timeline": timeline,
        "servers": servers,
        "client_groups": groups,
    }


def _demo_batch() -> RunBatch:
    return RunBatch(
        preset_name=EXAMPLE.name,
        runs=[RunResult(a, _sample_result(EXAMPLE, a)) for a in ALGORITHMS],
    )


def main() -> None:
    PRESETS_DIR.mkdir(parents=True, exist_ok=True)
    preset_path = PRESETS_DIR / "example.json"
    preset_path.write_text(
        json.dumps(EXAMPLE.to_dict(), indent=2, ensure_ascii=False), encoding="utf-8"
    )

    RUNS_DIR.mkdir(parents=True, exist_ok=True)
    run_path = RUNS_DIR / "demo-example.json"
    run_path.write_text(
        json.dumps(_demo_batch().to_dict(), indent=2, ensure_ascii=False), encoding="utf-8"
    )

    print(f"Записан пример пресета: {preset_path}")
    print(f"Записан пример прогона: {run_path}")


if __name__ == "__main__":
    main()
