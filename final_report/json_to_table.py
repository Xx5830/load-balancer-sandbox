#!/usr/bin/env python3
"""
Build an Excel workbook from one or more benchmark-preset JSON files,
with one sheet per algorithm found in the data.

Usage:
    python json_to_excel.py preset1.json preset2.json ...
    python json_to_excel.py path/to/folder_of_jsons/
    python json_to_excel.py *.json -o results.xlsx
    python json_to_excel.py *.json -a RoundRobin LeastConnections   # only these algorithms
"""

import argparse
import json
import re
import sys
from pathlib import Path

import pandas as pd


IGNORED_FIELDS = {
    "client_groups",
    "config",
    "experiment",
    "failures",
    "servers",
    "timeline",
    "totals"
}

IGNORED_PATHS = {
    "latency.count",
    "latency.max",
    "latency.min",
    "latency.histogram",
    "latency.stddev",
    "latency.unit"
}

KEEP_OVERRIDES = {
    "totals": ["throughput_rps"],
    "servers": ["peak_load"],
}

INVALID_SHEET_CHARS = re.compile(r"[:\\/?*\[\]]")


def flatten(value, prefix, out):
    # Recursively flatten dicts/lists into out[dotted_or_indexed_key] = leaf_value.
    if prefix in IGNORED_PATHS:
        return
    if isinstance(value, dict):
        if not value:
            out[prefix] = None
        else:
            for key, sub_value in value.items():
                flatten(sub_value, f"{prefix}.{key}" if prefix else str(key), out)
    elif isinstance(value, list):
        if not value:
            out[prefix] = None
        else:
            for i, item in enumerate(value):
                flatten(item, f"{prefix}[{i}]", out)
    else:
        out[prefix] = value


def collect_algorithms(data):
    return {run.get("algorithm") for run in data.get("runs", []) if "algorithm" in run}


def extract_row(data, algorithm):
    # Return a flattened {column: value} row for the requested algorithm, or None if absent.
    runs = data.get("runs", [])
    matches = [r for r in runs if r.get("algorithm") == algorithm]

    if not matches:
        return None
    if len(matches) > 1:
        print(
            f"  Warning: multiple runs with algorithm '{algorithm}' in '{data.get('preset_name')}'; using the first.",
            file=sys.stderr,
        )

    result = matches[0].get("result", {})
    row = {"name": data.get("preset_name")}
    for key, value in result.items():
        if key in IGNORED_FIELDS:
            continue
        flatten(value, key, row)

    for key, subkeys in KEEP_OVERRIDES.items():
        value = result.get(key)
        if isinstance(value, dict):
            for sk in subkeys:
                if sk in value:
                    flatten(value[sk], f"{key}.{sk}", row)
        elif isinstance(value, list):
            for i, item in enumerate(value):
                if isinstance(item, dict):
                    for sk in subkeys:
                        if sk in item:
                            flatten(item[sk], f"{key}[{i}].{sk}", row)

    return row


def gather_input_files(paths):
    files = []
    for p in paths:
        path = Path(p)
        if path.is_dir():
            files.extend(sorted(path.glob("*.json")))
        elif path.is_file():
            files.append(path)
        else:
            print(f"Warning: '{p}' not found, skipping.", file=sys.stderr)
    return files


def safe_sheet_name(name, used):
    # Excel sheet names: <=31 chars, no : \\ / ? * [ ], must be unique.
    cleaned = INVALID_SHEET_CHARS.sub("_", str(name))[:31] or "Sheet"
    candidate = cleaned
    n = 1
    while candidate in used:
        suffix = f"_{n}"
        candidate = cleaned[: 31 - len(suffix)] + suffix
        n += 1
    used.add(candidate)
    return candidate


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="JSON file(s) and/or folder(s) containing JSON files")
    parser.add_argument(
        "-a", "--algorithms", nargs="+",
        help="Only include these algorithm(s) (default: every algorithm found in the input files)",
    )
    parser.add_argument("-o", "--output", default="output.xlsx", help="Output .xlsx path (default: output.xlsx)")
    args = parser.parse_args()

    files = gather_input_files(args.inputs)
    if not files:
        print("No JSON files found.", file=sys.stderr)
        sys.exit(1)

    loaded = []
    all_algorithms = set()
    for path in files:
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception as e:
            print(f"Warning: could not read '{path}': {e}", file=sys.stderr)
            continue
        loaded.append((path, data))
        all_algorithms.update(collect_algorithms(data))

    if not loaded:
        print("No JSON files could be read.", file=sys.stderr)
        sys.exit(1)

    algorithms = sorted(args.algorithms) if args.algorithms else sorted(all_algorithms)
    if not algorithms:
        print("No algorithms found in the input files.", file=sys.stderr)
        sys.exit(1)

    used_sheet_names = set()
    sheets_written = 0
    with pd.ExcelWriter(args.output, engine="openpyxl") as writer:
        for algorithm in algorithms:
            rows = []
            for path, data in loaded:
                row = extract_row(data, algorithm)
                if row is None:
                    print(f"  Skipping '{path}' for algorithm '{algorithm}': no matching run.", file=sys.stderr)
                    continue
                rows.append(row)

            if not rows:
                print(f"No rows for algorithm '{algorithm}'; skipping sheet.", file=sys.stderr)
                continue

            df = pd.DataFrame(rows)
            df = df[["name"] + [c for c in df.columns if c != "name"]]  # keep "name" first

            sheet_name = safe_sheet_name(algorithm, used_sheet_names)
            df.to_excel(writer, sheet_name=sheet_name, index=False)
            sheets_written += 1
            print(f"Sheet '{sheet_name}': {len(df)} row(s) x {len(df.columns)} column(s)")

    if sheets_written == 0:
        print("Nothing written; no sheets had data.", file=sys.stderr)
        sys.exit(1)

    print(f"Wrote {sheets_written} sheet(s) to '{args.output}'")


if __name__ == "__main__":
    main()