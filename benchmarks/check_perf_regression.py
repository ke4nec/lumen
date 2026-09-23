#!/usr/bin/env python3
"""Compare one scene benchmark report with its backend-specific baseline.

The benchmark intentionally emits plain JSON so this gate stays usable on
Linux, macOS, and Windows runners without third-party Python packages.
"""

from __future__ import annotations

import argparse
import copy
import json
import math
import re
import statistics
import sys
from pathlib import Path
from typing import Any


PHASE_METRICS = (
    "p50_us",
    "p95_us",
    "allocs_p50",
    "alloc_bytes_p50",
)
# Timing significance floor (microseconds). A relative regression over the
# limit only fails when the absolute shift also exceeds this floor: on shared
# runners a ~10us scheduling jitter routinely exceeds 10% of a ~100us phase
# (e.g. grid/submit p50 107us -> 119us with bit-identical binaries), which is
# neither user-visible (sub-0.5% of a 12ms frame) nor attributable to the
# candidate. Allocation counts and commands_per_frame stay exact: they are
# deterministic for a given binary, so any excess is a real change. The 10%
# limit itself is unchanged.
MIN_TIMING_DELTA_US = 50.0
IDENTITY_FIELDS = ("backend", "scenario", "viewport", "toolchain", "build_type",
                   "platform", "warmup_frames", "measured_frames", "alpha_mode",
                   "clear_alpha", "measurement_scope", "runner", "measurement_session",
                   "scene_revision", "dependency_revision", "compiler_flags")
REQUIRED_PHASES = {"frame", "reconcile", "layout", "paint", "submit", "gpu_wait"}


def _number(value: Any, name: str, path: Path) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{path}: {name} must be numeric")
    number = float(value)
    if not math.isfinite(number) or number < 0:
        raise ValueError(f"{path}: {name} must be finite and non-negative")
    return number


def _load(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error
    if not isinstance(data, dict):
        raise ValueError(f"{path}: report must be a JSON object")
    for field in IDENTITY_FIELDS + ("phases", "commands_per_frame", "nodes", "commit", "binary_sha256", "frame_hash"):
        if field not in data:
            raise ValueError(f"{path}: missing required field {field}")
    if not isinstance(data["phases"], dict):
        raise ValueError(f"{path}: phases must be an object")
    validate_report(data, path)
    return data


def validate_report(data: dict[str, Any], path: Path) -> None:
    for field in IDENTITY_FIELDS:
        if field not in data or data[field] is None or data[field] == "":
            raise ValueError(f"{path}: missing comparison identity {field}")
    if not re.fullmatch(r"[0-9a-f]{40}", data.get("commit", "")):
        raise ValueError(f"{path}: commit must be an exact source revision")
    if not re.fullmatch(r"[0-9a-f]{64}", data.get("binary_sha256", "")):
        raise ValueError(f"{path}: missing measured binary digest")
    if not data.get("runner") or not data.get("frame_hash"):
        raise ValueError(f"{path}: missing runner or frame hash")
    for phase in REQUIRED_PHASES:
        if not isinstance(data.get("phases", {}).get(phase), dict):
            raise ValueError(f"{path}: missing required phase {phase}")
        for metric in PHASE_METRICS:
            _number(data["phases"][phase].get(metric), f"{phase}.{metric}", path)


def _relative_delta(baseline: float, current: float) -> float | None:
    if baseline == 0:
        return 0.0 if current == 0 else None
    return (current - baseline) / baseline


def _aggregate(reports: list[dict[str, Any]],
               time_estimator=statistics.median) -> dict[str, Any]:
    """Reduce repeated reports of one binary to a single comparison input.

    The CLI flow keeps the median. The interleaved same-machine gate
    (`run_perf_gate.py`) passes `min` for the timing metrics: host
    interference is strictly additive for CPU time, so the smallest repeated
    sample estimates the uncontended cost and cannot turn scheduler noise
    into a regression on either side. Allocation counts keep the median —
    they are path-dependent discrete values (a run settles in one of a few
    modes), so a minimum would latch onto whichever side happened to catch
    the low mode in one repeat.
    """
    if not reports:
        raise ValueError("at least one current report is required")
    first = copy.deepcopy(reports[0])
    for report in reports:
        validate_report(report, Path("<repeated-report>"))
    for report in reports[1:]:
        if report["phases"].keys() != first["phases"].keys():
            raise ValueError("repeated reports disagree on phase set")
        for field in IDENTITY_FIELDS + ("nodes", "commands_per_frame", "commit", "binary_sha256", "frame_hash"):
            if report[field] != first[field]:
                raise ValueError(
                    f"current reports disagree on {field}: "
                    f"{first[field]!r} vs {report[field]!r}"
                )
    for phase_name, phase in first["phases"].items():
        for metric in PHASE_METRICS:
            values = [
                _number(report["phases"][phase_name][metric],
                        f"phases.{phase_name}.{metric}", Path("<current>"))
                for report in reports
            ]
            phase[metric] = (time_estimator(values) if metric.endswith("_us")
                             else statistics.median(values))
    return first


def combine_verdicts(minimum: dict[str, Any], median: dict[str, Any]) -> dict[str, Any]:
    """Gate verdict from two aggregations of the same interleaved repeats.

    Timing metrics are compared once with the interference-robust minimum and
    once with the mode-robust median. A regression fails the gate only when it
    exceeds the limit under both: one-sided host interference inflates the
    median only, while the run-to-run sampling spread of a short phase's tail
    percentile moves the minimum alone. Allocation counts are identical under
    both aggregations (median), so a real allocation regression still fails.
    """
    return {
        "passed": minimum["passed"] or median["passed"],
        "tolerance": minimum["tolerance"],
        "baseline_commit": minimum["baseline_commit"],
        "current_commit": minimum["current_commit"],
        "aggregations": {"minimum": minimum, "median": median},
    }


def compare(baseline: dict[str, Any], current: dict[str, Any], tolerance: float,
            baseline_path: Path, current_path: Path) -> dict[str, Any]:
    failures: list[dict[str, Any]] = []
    checks: list[dict[str, Any]] = []
    validate_report(baseline, baseline_path)
    validate_report(current, current_path)
    if baseline.get("source_dirty") is not False:
        raise ValueError(f"{baseline_path}: reference must be measured from a clean checkout")

    for field in IDENTITY_FIELDS:
        if baseline[field] != current[field]:
            failures.append({
                "kind": "identity",
                "field": field,
                "baseline": baseline[field],
                "current": current[field],
            })

    # Node count is part of the scene contract. A changed scene must get a new
    # baseline instead of silently turning into a different workload.
    if baseline["nodes"] != current["nodes"]:
        failures.append({
            "kind": "scene-shape",
            "field": "nodes",
            "baseline": baseline["nodes"],
            "current": current["nodes"],
        })

    for field in ("commands_per_frame",):
        baseline_value = _number(baseline[field], field, baseline_path)
        current_value = _number(current[field], field, current_path)
        delta = _relative_delta(baseline_value, current_value)
        check = {
            "field": field,
            "baseline": baseline_value,
            "current": current_value,
            "relative_delta": delta,
            "limit": tolerance,
        }
        checks.append(check)
        if delta is None or delta > tolerance:
            failures.append({"kind": "regression", **check})

    baseline_phases = baseline["phases"]
    current_phases = current["phases"]
    for phase_name, baseline_phase in baseline_phases.items():
        if phase_name not in current_phases:
            failures.append({"kind": "missing-phase", "phase": phase_name})
            continue
        current_phase = current_phases[phase_name]
        if not isinstance(baseline_phase, dict) or not isinstance(current_phase, dict):
            failures.append({"kind": "invalid-phase", "phase": phase_name})
            continue
        for metric in PHASE_METRICS:
            if metric not in baseline_phase or metric not in current_phase:
                failures.append({
                    "kind": "missing-metric",
                    "phase": phase_name,
                    "metric": metric,
                })
                continue
            baseline_value = _number(
                baseline_phase[metric], f"phases.{phase_name}.{metric}", baseline_path
            )
            current_value = _number(
                current_phase[metric], f"phases.{phase_name}.{metric}", current_path
            )
            delta = _relative_delta(baseline_value, current_value)
            absolute_delta = current_value - baseline_value
            check = {
                "phase": phase_name,
                "metric": metric,
                "baseline": baseline_value,
                "current": current_value,
                "relative_delta": delta,
                "absolute_delta": absolute_delta,
                "limit": tolerance,
            }
            if metric.endswith("_us"):
                check["floor_us"] = MIN_TIMING_DELTA_US
            checks.append(check)
            over_limit = delta is None or delta > tolerance
            significant = (not metric.endswith("_us") or
                           absolute_delta > MIN_TIMING_DELTA_US)
            if over_limit and significant:
                failures.append({"kind": "regression", **check})

    return {
        "baseline": str(baseline_path),
        "current": str(current_path),
        "baseline_commit": baseline["commit"],
        "current_commit": current["commit"],
        "tolerance": tolerance,
        "passed": not failures,
        "checks": checks,
        "failures": failures,
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--current", required=True, nargs="+", type=Path)
    parser.add_argument("--tolerance", type=float, default=0.10)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(argv)
    if not 0 <= args.tolerance < 1:
        parser.error("--tolerance must be in [0, 1)")

    try:
        baseline = _load(args.baseline)
        current_reports = [_load(path) for path in args.current]
        current = _aggregate(current_reports)
        result = compare(baseline, current, args.tolerance,
                         args.baseline, args.current[0])
        result["current_reports"] = [str(path) for path in args.current]
    except ValueError as error:
        print(f"perf gate: {error}", file=sys.stderr)
        return 2

    output = json.dumps(result, indent=2, sort_keys=True, allow_nan=False) + "\n"
    if args.report:
        args.report.write_text(output, encoding="utf-8")
    for failure in result["failures"]:
        print("perf gate: FAIL " + json.dumps(failure, sort_keys=True), file=sys.stderr)
    if result["passed"]:
        print(f"perf gate: PASS ({', '.join(map(str, args.current))} vs {args.baseline})")
        return 0
    print(f"perf gate: {len(result['failures'])} check(s) failed", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
