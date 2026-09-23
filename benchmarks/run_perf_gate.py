"""Measure a pinned reference and the candidate alternately on the same runner."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import uuid
from check_perf_regression import _aggregate, combine_verdicts, compare

SCENARIOS = ("card-grid-6x8-1080p", "text-heavy", "grid", "virtual-list-1000", "semantics-diff")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_info(source, allow_dirty):
    commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    dirty = bool(subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True).strip())
    if dirty and not allow_dirty:
        raise ValueError(f"tracked source changes in {source}; build a clean revision")
    return dict(commit=commit, source_dirty=dirty,
                scene_revision=digest(source / "benchmarks/scene_bench.cpp"),
                dependency_revision=digest(source / "cmake/dependencies.cmake"))


def compiler_flags(binary):
    cache = binary.parent.parent / "CMakeCache.txt"
    flags = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(("CMAKE_CXX_FLAGS:", "CMAKE_CXX_FLAGS_RELEASE:", "CMAKE_CXX_COMPILER:")):
            name, value = line.split("=", 1)
            flags[name] = value
    if len(flags) != 3:
        raise ValueError(f"cannot establish compiler flags from {cache}")
    return flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("cpu", "skia", "gpu"), required=True)
    parser.add_argument("--baseline-source", type=Path, required=True)
    parser.add_argument("--baseline-bin", type=Path, required=True)
    parser.add_argument("--current-source", type=Path, default=Path("."))
    parser.add_argument("--current-bin", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-dirty-current", action="store_true")
    parser.add_argument("--scenarios", nargs="+", choices=SCENARIOS, default=SCENARIOS,
                        help="diagnostic subset; CI always runs the default full set")
    args = parser.parse_args()
    manifest = json.loads((Path(__file__).resolve().parent.parent / "docs/perf-baselines/ci/reference.json").read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    # CPU/raster scene measurement is single-threaded. Keep both versions on
    # one allowed logical CPU to avoid migration across heterogeneous cores.
    affinity = None
    if hasattr(os, "sched_getaffinity"):
        allowed = sorted(os.sched_getaffinity(0))
        if args.backend != "gpu":
            os.sched_setaffinity(0, {allowed[0]})
        affinity = sorted(os.sched_getaffinity(0))
    cpu = platform.processor()
    if Path("/proc/cpuinfo").exists():
        cpu = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                    if line.startswith("model name")), cpu)
    runner = dict(os=platform.system(), release=platform.release(), arch=platform.machine(),
                  cpu=cpu, cpu_count=os.cpu_count(), affinity=affinity,
                  runner=os.environ.get("RUNNER_NAME", platform.node()))
    if args.backend == "gpu":
        runner["gl_info"] = subprocess.check_output(["glxinfo", "-B"], text=True)
    session = str(uuid.uuid4())
    metadata = {}
    binaries = {"baseline": args.baseline_bin.resolve(), "current": args.current_bin.resolve()}
    for name, source in (("baseline", args.baseline_source), ("current", args.current_source)):
        metadata[name] = source_info(source, name == "current" and args.allow_dirty_current)
        metadata[name].update(runner=runner, measurement_session=session,
                              binary_sha256=digest(binaries[name]), compiler_flags=compiler_flags(binaries[name]))
    if metadata["baseline"]["commit"] != manifest["commit"]:
        raise ValueError("reference checkout does not match the reviewed baseline commit")
    failed = False
    repeats = int(manifest.get("repeats", 3))
    for scenario in args.scenarios:
        reports = {"baseline": [], "current": []}
        for repeat in range(repeats):
            # Alternate order to reduce warm-machine and scheduling bias.
            for name in (("baseline", "current") if repeat % 2 == 0 else ("current", "baseline")):
                environment = os.environ.copy()
                # Toolchain/build type must come from the binary, not caller labels.
                environment.pop("LUMEN_BENCH_TOOLCHAIN", None)
                environment.pop("LUMEN_BENCH_BUILD_TYPE", None)
                environment["LUMEN_BENCH_COMMIT"] = metadata[name]["commit"]
                raw = subprocess.check_output([str(binaries[name]), "--backend", args.backend,
                    "--scenario", scenario, "--warmup", str(manifest["warmup_frames"]),
                    "--frames", str(manifest["measured_frames"]), "--json"],
                    env=environment, text=True, timeout=180)
                report = json.loads(raw)
                report.update(metadata[name])
                path = args.output / f"{name}-{scenario}-{repeat}.json"
                path.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
                reports[name].append(report)
        # Timing metrics are judged twice: the interference-robust minimum of
        # the interleaved repeats (VM host noise only ever adds CPU time) and
        # the mode-robust median (a short phase's tail percentile has wide
        # run-to-run sampling spread). Only a regression over the limit under
        # BOTH aggregations fails the gate; the 10% limit stays unchanged.
        # Allocation counts keep the median in both (see _aggregate) because
        # they are discrete, path-dependent values.
        verdict = combine_verdicts(
            compare(_aggregate(reports["baseline"], min),
                    _aggregate(reports["current"], min),
                    manifest["tolerance"], args.baseline_bin, args.current_bin),
            compare(_aggregate(reports["baseline"]), _aggregate(reports["current"]),
                    manifest["tolerance"], args.baseline_bin, args.current_bin))
        (args.output / f"gate-{scenario}.json").write_text(json.dumps(verdict, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        failed = failed or not verdict["passed"]
        estimators = "+".join(name for name, res in verdict["aggregations"].items() if res["passed"]) or "none"
        print(f"{args.backend}/{scenario}: {'PASS' if verdict['passed'] else 'FAIL'} (limit held under: {estimators})", flush=True)
        if not verdict["passed"]:
            # Inline the worst offenders so a failure is diagnosable from the
            # step log alone; the full comparison stays in gate-<scenario>.json.
            shown = 0
            for agg_name in ("minimum", "median"):
                for failure in verdict["aggregations"][agg_name]["failures"]:
                    if shown >= 10 or failure.get("kind") != "regression":
                        continue
                    print(f"  FAIL detail [{agg_name}]: {json.dumps(failure, sort_keys=True)}", flush=True)
                    shown += 1
                if shown >= 10:
                    break
    return 1 if failed else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        raise SystemExit(f"perf gate cannot compare: {error}")
