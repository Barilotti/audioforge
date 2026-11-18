#!/usr/bin/env python3
"""Compares a Google Benchmark JSON report with a committed baseline (ADR-0017).

Times are normalised by BM_Calibration from the same run, so runners of different speed can
share a baseline. A benchmark fails when its normalised median exceeds the baseline by more
than the tolerance. Use --write-baseline to record a new baseline from a report.
"""

import argparse
import json
import re
import sys

CALIBRATION = "BM_Calibration"
SAMPLE_RATE = 48_000


def medians(report_path):
    with open(report_path, encoding="utf-8") as f:
        report = json.load(f)
    result = {}
    for b in report["benchmarks"]:
        if b.get("aggregate_name") == "median":
            result[b["run_name"]] = b["real_time"] * unit_seconds(b["time_unit"])
        elif b.get("run_type") == "iteration" and b["run_name"] not in result:
            # A run without repetitions: a single measurement.
            result[b["run_name"]] = b["real_time"] * unit_seconds(b["time_unit"])
    if CALIBRATION not in result:
        sys.exit(f"{report_path}: {CALIBRATION} is missing")
    return result


def unit_seconds(unit):
    return {"ns": 1e-9, "us": 1e-6, "ms": 1e-3, "s": 1.0}[unit]


def realtime_load(name, seconds):
    """Share of the block duration spent rendering, for BM_Engine*/<block> benchmarks."""
    match = re.fullmatch(r"BM_Engine\w*/(\d+)", name)
    return seconds / (int(match.group(1)) / SAMPLE_RATE) if match else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("report")
    parser.add_argument("baseline")
    parser.add_argument("--tolerance", type=float, default=None,
                        help="allowed slowdown as a fraction; default: the baseline's (0.25)")
    parser.add_argument("--write-baseline", action="store_true")
    args = parser.parse_args()

    times = medians(args.report)
    calibration = times[CALIBRATION]
    ratios = {name: t / calibration for name, t in times.items() if name != CALIBRATION}

    if args.write_baseline:
        with open(args.baseline, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"tolerance": args.tolerance or 0.25, "ratios": ratios}, f, indent=2, sort_keys=True)
            f.write("\n")
        print(f"Baseline written: {args.baseline}")
        return 0

    with open(args.baseline, encoding="utf-8") as f:
        baseline = json.load(f)
    tolerance = args.tolerance if args.tolerance is not None else baseline.get("tolerance", 0.25)
    failures = []
    print(f"{'benchmark':40} {'time':>12} {'ratio':>10} {'baseline':>10} {'change':>8} {'load':>7}")
    for name, ratio in sorted(ratios.items()):
        expected = baseline["ratios"].get(name)
        load = realtime_load(name, times[name])
        load_text = f"{load:.1%}" if load is not None else ""
        if expected is None:
            print(f"{name:40} {times[name] * 1e6:10.1f}us {ratio:10.3f} {'new':>10} {'':>8} {load_text:>7}")
            continue
        change = ratio / expected - 1.0
        flag = "  REGRESSION" if change > tolerance else ""
        print(f"{name:40} {times[name] * 1e6:10.1f}us {ratio:10.3f} {expected:10.3f} "
              f"{change:+8.1%} {load_text:>7}{flag}")
        if flag:
            failures.append(name)
    for name in sorted(set(baseline["ratios"]) - set(ratios)):
        print(f"{name:40} missing from the report")
        failures.append(name)

    if failures:
        print(f"\n{len(failures)} benchmark(s) slower than the baseline by more than "
              f"{tolerance:.0%}: {', '.join(failures)}")
        print("If the slowdown is intended, record a new baseline (docs/testing/benchmarks.md).")
        return 1
    print(f"\nNo regression beyond {tolerance:.0%}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
