"""Validate and compare the four DP-8 Designer pipeline workloads."""

from __future__ import annotations

import argparse
import copy
import json
import re
import statistics
import sys
from pathlib import Path

from check_perf_regression import MIN_TIMING_DELTA_US, PHASE_METRICS, _number, _relative_delta


COMMON_PHASES = {"serialize", "read", "schema", "compile", "layout", "paint"}
FIXTURES = {
    "l0_12": (12, {"import"}),
    "edit_100": (100, {"edit"}),
    "outline_1000": (1000, {"outline"}),
    "virtual_list_1000": (1, set()),
}
HEAP_METRICS = ("allocs_p50", "alloc_bytes_p50", "peak_bytes_p50", "live_bytes_p50")
SHAPE_METRICS = ("render_nodes", "materialized_items", "rebuilds")
IDENTITY_FIELDS = ("schema", "scope", "toolchain", "build_type", "viewport", "warmup",
                   "iterations", "runner", "measurement_session", "fixture_revision",
                   "dependency_revision", "compiler_flags")
BUILD_FLAGS = ("CMAKE_CXX_COMPILER", "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELEASE",
               "LUMEN_ENABLE_SKIA", "LUMEN_ENABLE_GPU", "LUMEN_ENABLE_ACCESSIBILITY_BRIDGE")


def metric_number(value, name, path):
    try:
        return _number(value, name, path)
    except OverflowError as error:
        raise ValueError(f"{path}: {name} exceeds finite metric range") from error


def validate_report(data, path):
    if not isinstance(data, dict):
        raise ValueError(f"{path}: report must be an object")
    for field in IDENTITY_FIELDS:
        if field not in data or data[field] is None or data[field] == "":
            raise ValueError(f"{path}: missing comparison identity {field}")
    if type(data["schema"]) is not int or data["schema"] != 2:
        raise ValueError(f"{path}: expected report schema 2")
    if data["scope"] != "designer_pipeline_cpp_heap" or data["build_type"] != "Release":
        raise ValueError(f"{path}: expected Release Designer pipeline measurement")
    if data["viewport"] != [800, 600]:
        raise ValueError(f"{path}: unexpected fixture viewport")
    for field, lower in (("warmup", 0), ("iterations", 1)):
        if type(data[field]) is not int or not lower <= data[field] <= 1000:
            raise ValueError(f"{path}: invalid {field}")
    for field, length in (("commit", 40), ("binary_sha256", 64),
                          ("fixture_revision", 64), ("dependency_revision", 64)):
        if not isinstance(data.get(field), str) or not re.fullmatch(
                rf"[0-9a-f]{{{length}}}", data[field]):
            raise ValueError(f"{path}: invalid source/binary identity {field}")
    if type(data.get("source_dirty")) is not bool:
        raise ValueError(f"{path}: missing source_dirty")
    if not isinstance(data["toolchain"], str) or not isinstance(data["measurement_session"], str):
        raise ValueError(f"{path}: invalid toolchain/session identity")
    runner = data["runner"]
    if not isinstance(runner, dict) or any(field not in runner for field in
            ("os", "release", "arch", "cpu", "cpu_count", "affinity", "runner")):
        raise ValueError(f"{path}: missing runner identity")
    flags = data["compiler_flags"]
    if not isinstance(flags, dict) or any(not isinstance(flags.get(field), str) for field in BUILD_FLAGS):
        raise ValueError(f"{path}: missing compiler flags")
    fixtures = data.get("fixtures")
    if not isinstance(fixtures, dict) or fixtures.keys() != FIXTURES.keys():
        raise ValueError(f"{path}: expected all four Designer fixtures")
    for name, (nodes, extra) in FIXTURES.items():
        fixture = fixtures[name]
        if not isinstance(fixture, dict):
            raise ValueError(f"{path}: invalid fixture {name}")
        if type(fixture.get("document_nodes")) is not int or fixture["document_nodes"] != nodes:
            raise ValueError(f"{path}: changed document shape for {name}")
        if not isinstance(fixture.get("frame_hash"), str) or not re.fullmatch(
                r"[0-9a-f]{16}", fixture["frame_hash"]) or int(fixture["frame_hash"], 16) == 0:
            raise ValueError(f"{path}: invalid {name}.frame_hash")
        for metric in SHAPE_METRICS:
            if type(fixture.get(metric)) is not int or fixture[metric] < 0:
                raise ValueError(f"{path}: invalid {name}.{metric}")
        if fixture["render_nodes"] == 0 or fixture["rebuilds"] == 0:
            raise ValueError(f"{path}: empty {name} workload")
        if name == "virtual_list_1000":
            if fixture["materialized_items"] == 0 or fixture["render_nodes"] != fixture["materialized_items"] + 1:
                raise ValueError(f"{path}: invalid virtualized workload")
        elif fixture["render_nodes"] != nodes or fixture["materialized_items"] != 0:
            raise ValueError(f"{path}: changed render shape for {name}")
        phases = fixture.get("phases")
        if not isinstance(phases, dict) or phases.keys() != COMMON_PHASES | extra:
            raise ValueError(f"{path}: missing or changed phases for {name}")
        for phase, metrics in phases.items():
            if not isinstance(metrics, dict):
                raise ValueError(f"{path}: invalid {name}.{phase}")
            for metric in PHASE_METRICS:
                metric_number(metrics.get(metric), f"{name}.{phase}.{metric}", path)
            if metrics["p95_us"] < metrics["p50_us"]:
                raise ValueError(f"{path}: {name}.{phase} p95 below p50")
        heap = fixture.get("heap")
        if not isinstance(heap, dict):
            raise ValueError(f"{path}: missing heap for {name}")
        for metric in HEAP_METRICS:
            metric_number(heap.get(metric), f"{name}.heap.{metric}", path)
        if any(heap[metric] == 0 for metric in ("allocs_p50", "alloc_bytes_p50", "peak_bytes_p50")):
            raise ValueError(f"{path}: empty {name} heap measurement")
        if heap["live_bytes_p50"] > heap["peak_bytes_p50"]:
            raise ValueError(f"{path}: {name} live heap exceeds peak")


def load(path):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error
    validate_report(data, path)
    return data


def aggregate(reports, time_estimator=statistics.median):
    if not reports:
        raise ValueError("at least one report is required")
    first = copy.deepcopy(reports[0])
    for report in reports:
        validate_report(report, Path("<repeat>"))
        for field in IDENTITY_FIELDS + ("commit", "binary_sha256", "source_dirty"):
            if report[field] != first[field]:
                raise ValueError(f"repeated reports disagree on {field}")
        for name in FIXTURES:
            for field in ("document_nodes", "frame_hash") + SHAPE_METRICS:
                if report["fixtures"][name][field] != first["fixtures"][name][field]:
                    raise ValueError(f"repeated reports disagree on {name}.{field}")
    for name, fixture in first["fixtures"].items():
        for phase, metrics in fixture["phases"].items():
            for metric in PHASE_METRICS:
                values = [report["fixtures"][name]["phases"][phase][metric] for report in reports]
                metrics[metric] = (time_estimator(values) if metric.endswith("_us")
                                   else statistics.median(values))
        for metric in HEAP_METRICS:
            fixture["heap"][metric] = statistics.median(
                report["fixtures"][name]["heap"][metric] for report in reports)
    return first


def compare(baseline, current, tolerance, baseline_path, current_path):
    validate_report(baseline, baseline_path)
    validate_report(current, current_path)
    if not 0 <= tolerance < 1:
        raise ValueError("tolerance must be in [0, 1)")
    if baseline["source_dirty"]:
        raise ValueError(f"{baseline_path}: reference must be a clean checkout")
    failures = []
    checks = []
    for field in IDENTITY_FIELDS:
        if baseline[field] != current[field]:
            failures.append(dict(kind="identity", field=field,
                                 baseline=baseline[field], current=current[field]))
    for name in FIXTURES:
        before, after = baseline["fixtures"][name], current["fixtures"][name]
        metrics = [(field, before[field], after[field]) for field in SHAPE_METRICS]
        metrics += [(f"heap.{field}", before["heap"][field], after["heap"][field])
                    for field in HEAP_METRICS]
        metrics += [(f"{phase}.{metric}", values[metric], after["phases"][phase][metric])
                    for phase, values in before["phases"].items() for metric in PHASE_METRICS]
        for field, old, new in metrics:
            delta = _relative_delta(old, new)
            check = dict(fixture=name, field=field, baseline=old, current=new,
                         relative_delta=delta, absolute_delta=new - old, limit=tolerance)
            timing = field.endswith("_us")
            if timing:
                check["floor_us"] = MIN_TIMING_DELTA_US
            checks.append(check)
            # Compare the values at the boundary, avoiding division rounding
            # that can turn an exact 10% allocation increase into a failure.
            if new > old * (1 + tolerance) and (not timing or new - old > MIN_TIMING_DELTA_US):
                failures.append(dict(kind="regression", **check))
    return dict(passed=not failures, baseline=str(baseline_path), current=str(current_path),
                baseline_commit=baseline["commit"], current_commit=current["commit"],
                tolerance=tolerance, checks=checks, failures=failures)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--current", type=Path, nargs="+", required=True)
    parser.add_argument("--tolerance", type=float, default=0.1)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(argv)
    try:
        verdict = compare(load(args.baseline), aggregate([load(path) for path in args.current]),
                          args.tolerance, args.baseline, args.current[0])
        if args.report:
            args.report.write_text(json.dumps(verdict, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    except (ValueError, OSError) as error:
        print(f"designer perf gate: {error}", file=sys.stderr)
        return 2
    for failure in verdict["failures"]:
        print(json.dumps(failure, allow_nan=False), file=sys.stderr)
    print(f"designer perf gate: {'PASS' if verdict['passed'] else 'FAIL'}")
    return 0 if verdict["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
