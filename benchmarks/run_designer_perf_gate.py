"""Measure the pinned DP-8 reference and candidate alternately on one runner."""

import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import uuid

from check_designer_perf_regression import BUILD_FLAGS, aggregate, compare, validate_report
from check_perf_regression import combine_verdicts
from run_perf_gate import digest


MANIFEST = Path(__file__).resolve().parent.parent / "docs/perf-baselines/designer/reference.json"


def source_info(source, allow_dirty):
    commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(
        ["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True).strip())
    if dirty and not allow_dirty:
        raise ValueError(f"tracked source changes in {source}; build a clean revision")
    return dict(commit=commit, source_dirty=dirty,
                fixture_revision=digest(source / "benchmarks/designer_bench.cpp"),
                dependency_revision=digest(source / "cmake/dependencies.cmake"))


def build_info(binary, source):
    cache = next((parent / "CMakeCache.txt" for parent in binary.parents
                  if (parent / "CMakeCache.txt").is_file()), None)
    if cache is None:
        raise ValueError(f"cannot find CMake build provenance for {binary}")
    values = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith(("#", "//")) and "=" in line and ":" in line.split("=", 1)[0]:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    if not values.get("CMAKE_HOME_DIRECTORY") or Path(values["CMAKE_HOME_DIRECTORY"]).resolve() != source.resolve():
        raise ValueError(f"{binary}: build source does not match {source}")
    if any(key not in values for key in BUILD_FLAGS):
        raise ValueError(f"{cache}: incomplete compiler/build flags")
    return {key: values[key] for key in BUILD_FLAGS}


def run(args):
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    args.output.mkdir(parents=True, exist_ok=True)
    affinity = None
    if hasattr(os, "sched_getaffinity"):
        allowed = sorted(os.sched_getaffinity(0))
        os.sched_setaffinity(0, {allowed[0]})
        affinity = sorted(os.sched_getaffinity(0))
    cpu = platform.processor()
    if Path("/proc/cpuinfo").exists():
        cpu = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                    if line.startswith("model name")), cpu)
    runner = dict(os=platform.system(), release=platform.release(), arch=platform.machine(),
                  cpu=cpu, cpu_count=os.cpu_count(), affinity=affinity,
                  runner=os.environ.get("RUNNER_NAME", platform.node()))
    session = str(uuid.uuid4())
    binaries = {"baseline": args.baseline_bin.resolve(), "current": args.current_bin.resolve()}
    metadata = {}
    for name, source in (("baseline", args.baseline_source), ("current", args.current_source)):
        metadata[name] = source_info(source, name == "current" and args.allow_dirty_current)
        metadata[name].update(runner=runner, measurement_session=session,
                              binary_sha256=digest(binaries[name]), compiler_flags=build_info(binaries[name], source))
    if metadata["baseline"]["commit"] != manifest["commit"]:
        raise ValueError("reference checkout does not match the reviewed baseline commit")
    reports = {"baseline": [], "current": []}
    for repeat in range(manifest["repeats"]):
        for name in (("baseline", "current") if repeat % 2 == 0 else ("current", "baseline")):
            raw = subprocess.check_output([str(binaries[name]), "--benchmark", "--warmup", str(manifest["warmup"]),
                                           "--iterations", str(manifest["iterations"])], text=True, timeout=180)
            report = json.loads(raw)
            if not isinstance(report, dict):
                raise ValueError(f"{binaries[name]}: benchmark report must be an object")
            report.update(metadata[name])
            validate_report(report, binaries[name])
            (args.output / f"{name}-{repeat}.json").write_text(
                json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
            reports[name].append(report)
            print(f"designer/{name}: measured {repeat + 1}/{manifest['repeats']}", flush=True)
    verdict = combine_verdicts(
        compare(aggregate(reports["baseline"], min), aggregate(reports["current"], min),
                manifest["tolerance"], args.baseline_bin, args.current_bin),
        compare(aggregate(reports["baseline"]), aggregate(reports["current"]),
                manifest["tolerance"], args.baseline_bin, args.current_bin))
    (args.output / "gate.json").write_text(json.dumps(verdict, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"designer/four-fixtures: {'PASS' if verdict['passed'] else 'FAIL'}", flush=True)
    if not verdict["passed"]:
        for estimator, result in verdict["aggregations"].items():
            for failure in result["failures"][:10]:
                print(f"{estimator}: {json.dumps(failure, allow_nan=False)}", flush=True)
    return 0 if verdict["passed"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-source", type=Path, required=True)
    parser.add_argument("--baseline-bin", type=Path, required=True)
    parser.add_argument("--current-source", type=Path, default=Path("."))
    parser.add_argument("--current-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-dirty-current", action="store_true")
    args = parser.parse_args()
    try:
        return run(args)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"designer perf gate: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
