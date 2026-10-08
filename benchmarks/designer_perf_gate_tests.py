"""DP-8 gate behavior, provenance, CLI exits, and interleaved runner tests."""

import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from check_designer_perf_regression import (COMMON_PHASES, FIXTURES, HEAP_METRICS,
                                            aggregate, compare, validate_report)
from check_perf_regression import combine_verdicts
import run_designer_perf_gate as runner


SCRIPT = Path(__file__).with_name("check_designer_perf_regression.py")


def report():
    data = dict(schema=2, scope="designer_pipeline_cpp_heap", build_type="Release",
                toolchain="GNU-test", viewport=[800, 600], warmup=3, iterations=20,
                commit="a" * 40, binary_sha256="b" * 64, source_dirty=False,
                fixture_revision="c" * 64, dependency_revision="d" * 64,
                runner=dict(os="Linux", release="test", arch="x86_64", cpu="test",
                            cpu_count=1, affinity=[0], runner="test"), measurement_session="test-session",
                compiler_flags=dict(CMAKE_CXX_COMPILER="/usr/bin/c++", CMAKE_CXX_FLAGS="",
                                    CMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG", LUMEN_ENABLE_SKIA="OFF",
                                    LUMEN_ENABLE_GPU="OFF", LUMEN_ENABLE_ACCESSIBILITY_BRIDGE="OFF"), fixtures={})
    for name, (nodes, extra) in FIXTURES.items():
        virtual = name == "virtual_list_1000"
        data["fixtures"][name] = dict(
            document_nodes=nodes, render_nodes=21 if virtual else nodes,
            materialized_items=20 if virtual else 0, rebuilds=2 if name == "edit_100" else 1,
            frame_hash="e" * 16, heap={metric: 1000 for metric in HEAP_METRICS},
            phases={phase: dict(p50_us=1000, p95_us=2000, allocs_p50=100, alloc_bytes_p50=1000)
                    for phase in COMMON_PHASES | extra})
    return data


def comparison(before, after):
    return compare(before, after, 0.1, Path("baseline"), Path("current"))


class DesignerGateTests(unittest.TestCase):
    def test_each_phase_and_heap_metric_is_gated_at_ten_percent(self):
        for name in FIXTURES:
            before = report()
            targets = [("heap", metric) for metric in HEAP_METRICS]
            targets += [(phase, metric) for phase in before["fixtures"][name]["phases"]
                        for metric in ("p50_us", "p95_us", "allocs_p50", "alloc_bytes_p50")]
            for group, metric in targets:
                for multiplier, passed in ((1.1, True), (1.2, False)):
                    with self.subTest(fixture=name, group=group, metric=metric, multiplier=multiplier):
                        after = copy.deepcopy(before)
                        values = (after["fixtures"][name]["heap"] if group == "heap"
                                  else after["fixtures"][name]["phases"][group])
                        # Keep the heap ordering valid when varying either endpoint.
                        if group == "heap" and metric == "live_bytes_p50":
                            before["fixtures"][name]["heap"]["live_bytes_p50"] = 100
                            values["live_bytes_p50"] = 100
                        values[metric] *= multiplier
                        self.assertEqual(comparison(before, after)["passed"], passed)

    def test_timing_floor_and_zero_to_positive_are_explicit(self):
        before, after = report(), report()
        before["fixtures"]["l0_12"]["phases"]["paint"]["p50_us"] = 100
        after["fixtures"]["l0_12"]["phases"]["paint"]["p50_us"] = 150
        self.assertTrue(comparison(before, after)["passed"])
        after["fixtures"]["l0_12"]["phases"]["paint"]["p50_us"] = 151
        self.assertFalse(comparison(before, after)["passed"])
        before, after = report(), report()
        before["fixtures"]["edit_100"]["heap"]["live_bytes_p50"] = 0
        verdict = comparison(before, after)
        self.assertFalse(verdict["passed"])
        self.assertIsNone(verdict["failures"][0]["relative_delta"])
        json.dumps(verdict, allow_nan=False)

    def test_rebuilds_and_virtual_materialization_cannot_silently_grow(self):
        before, after = report(), report()
        after["fixtures"]["edit_100"]["rebuilds"] += 1
        self.assertFalse(comparison(before, after)["passed"])
        after = report()
        after["fixtures"]["virtual_list_1000"].update(render_nodes=101, materialized_items=100)
        self.assertFalse(comparison(before, after)["passed"])

    def test_missing_invalid_metrics_and_changed_workload_are_rejected(self):
        invalid = [None, True, "100", -1, math.nan, math.inf, 10 ** 400]
        for value in invalid:
            with self.subTest(value=value):
                data = report()
                data["fixtures"]["l0_12"]["phases"]["paint"]["p50_us"] = value
                with self.assertRaises(ValueError):
                    validate_report(data, Path("test"))
        for mutation in (
                lambda d: d["fixtures"].pop("l0_12"),
                lambda d: d["fixtures"]["l0_12"]["phases"].pop("import"),
                lambda d: d["fixtures"]["l0_12"]["heap"].pop("peak_bytes_p50"),
                lambda d: d["fixtures"]["l0_12"].update(document_nodes=13),
                lambda d: d.update(build_type="Unspecified"),
                lambda d: d.update(source_dirty=None),
                lambda d: d.update(binary_sha256="unknown")):
            data = report()
            mutation(data)
            with self.assertRaises(ValueError):
                validate_report(data, Path("test"))

    def test_identity_mismatch_and_dirty_reference_fail(self):
        for field, value in (("toolchain", "Other"), ("fixture_revision", "f" * 64),
                             ("dependency_revision", "f" * 64), ("warmup", 4),
                             ("runner", report()["runner"] | {"cpu": "different"}),
                             ("measurement_session", "other"),
                             ("compiler_flags", report()["compiler_flags"] | {"CMAKE_CXX_FLAGS_RELEASE": "-O0"})):
            with self.subTest(field=field):
                after = report()
                after[field] = value
                self.assertFalse(comparison(report(), after)["passed"])
        before = report()
        before["source_dirty"] = True
        with self.assertRaises(ValueError):
            comparison(before, report())

    def test_repeated_frame_and_source_identity_must_stay_stable(self):
        for field in ("frame_hash", "render_nodes", "rebuilds"):
            after = report()
            fixture = after["fixtures"]["virtual_list_1000"]
            fixture[field] = "f" * 16 if field == "frame_hash" else fixture[field] + 1
            if field == "render_nodes":
                fixture["materialized_items"] += 1
            with self.assertRaises(ValueError):
                aggregate([report(), after])
        after = report()
        after["binary_sha256"] = "f" * 64
        with self.assertRaises(ValueError):
            aggregate([report(), after])
        # Cross-version frame hashes may change; determinism is per binary.
        after = report()
        after["fixtures"]["l0_12"]["frame_hash"] = "f" * 16
        self.assertTrue(comparison(report(), after)["passed"])

    def test_aggregation_keeps_allocation_median_and_requires_both_verdicts(self):
        samples = [report() for _ in range(3)]
        for data, timing, allocations in zip(samples, (1000, 1200, 2000), (100, 120, 200)):
            data["fixtures"]["l0_12"]["phases"]["paint"].update(p50_us=timing, allocs_p50=allocations)
        minimum = aggregate(samples, min)
        median = aggregate(samples)
        self.assertEqual(minimum["fixtures"]["l0_12"]["phases"]["paint"]["p50_us"], 1000)
        self.assertEqual(median["fixtures"]["l0_12"]["phases"]["paint"]["p50_us"], 1200)
        self.assertEqual(minimum["fixtures"]["l0_12"]["phases"]["paint"]["allocs_p50"], 120)
        self.assertFalse(combine_verdicts(comparison(report(), minimum), comparison(report(), median))["passed"])

    def test_cli_returns_pass_regression_and_invalid_input_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            before, after, output = root / "baseline.json", root / "current.json", root / "gate.json"
            before.write_text(json.dumps(report()), encoding="utf-8")
            for multiplier, status in ((1.1, 0), (1.2, 1)):
                data = report()
                data["fixtures"]["l0_12"]["heap"]["peak_bytes_p50"] *= multiplier
                after.write_text(json.dumps(data), encoding="utf-8")
                result = subprocess.run([sys.executable, "-B", str(SCRIPT), "--baseline", str(before),
                                         "--current", str(after), "--report", str(output)],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, status, result.stderr)
                self.assertEqual(json.loads(output.read_text())["passed"], status == 0)
            for contents in ("{", "[]", json.dumps({}), json.dumps(report()).replace('"p50_us": 1000', '"p50_us": NaN')):
                after.write_text(contents, encoding="utf-8")
                result = subprocess.run([sys.executable, "-B", str(SCRIPT), "--baseline", str(before),
                                         "--current", str(after)], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 2, result.stderr)

    def test_runner_alternates_and_preserves_raw_provenance(self):
        manifest = json.loads(runner.MANIFEST.read_text())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = SimpleNamespace(output=root / "output", baseline_source=root / "baseline-src",
                                   current_source=root / "current-src", baseline_bin=root / "baseline-bin",
                                   current_bin=root / "current-bin", allow_dirty_current=False)
            calls = []

            def measure(command, **kwargs):
                calls.append(Path(command[0]).name)
                data = report()
                data.update(warmup=manifest["warmup"], iterations=manifest["iterations"])
                self.assertEqual(command[2:], ["--warmup", str(manifest["warmup"]),
                                               "--iterations", str(manifest["iterations"])])
                return json.dumps(data)

            def source(source, allow_dirty):
                data = report()
                return {key: (manifest["commit"] if key == "commit" else data[key])
                        for key in ("commit", "source_dirty", "fixture_revision", "dependency_revision")}

            with patch.object(runner, "source_info", side_effect=source), \
                    patch.object(runner, "build_info", return_value=report()["compiler_flags"]), \
                    patch.object(runner, "digest", return_value="b" * 64), \
                    patch.object(runner.platform, "processor", return_value="test"), \
                    patch.object(runner.subprocess, "check_output", side_effect=measure), \
                    patch.object(runner.os, "sched_setaffinity", create=True):
                self.assertEqual(runner.run(args), 0)
            self.assertEqual(calls, [name for repeat in range(5) for name in
                                    (("baseline-bin", "current-bin") if repeat % 2 == 0
                                     else ("current-bin", "baseline-bin"))])
            self.assertEqual(len(list(args.output.glob("*-*.json"))), 10)
            measured = json.loads((args.output / "baseline-0.json").read_text())
            self.assertEqual(measured["commit"], manifest["commit"])
            self.assertTrue(measured["runner"]["cpu_count"])
            self.assertTrue(measured["measurement_session"])
            self.assertTrue(json.loads((args.output / "gate.json").read_text())["passed"])

    def test_build_cache_checks_source_and_supports_config_subdirectories(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "CMakeCache.txt"
            cache.write_text("\n".join([
                f"CMAKE_HOME_DIRECTORY:INTERNAL={root / 'source'}",
                "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++", "CMAKE_CXX_FLAGS:STRING=",
                "CMAKE_CXX_FLAGS_RELEASE:STRING=-O3 -DNDEBUG", "LUMEN_ENABLE_SKIA:BOOL=OFF",
                "LUMEN_ENABLE_GPU:BOOL=OFF", "LUMEN_ENABLE_ACCESSIBILITY_BRIDGE:BOOL=OFF"]), encoding="utf-8")
            binary = root / "benchmarks/Release/lumen-designer-bench"
            self.assertEqual(runner.build_info(binary, root / "source")["CMAKE_CXX_FLAGS_RELEASE"], "-O3 -DNDEBUG")
            with self.assertRaises(ValueError):
                runner.build_info(binary, root / "other")
            cache.write_text(cache.read_text().replace(f"CMAKE_HOME_DIRECTORY:INTERNAL={root / 'source'}", ""))
            with self.assertRaises(ValueError):
                runner.build_info(binary, Path.cwd())

    def test_dirty_checkout_needs_explicit_local_diagnostic_opt_in(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(runner.subprocess, "check_output", side_effect=["a" * 40, " M tracked.cpp"]):
                with self.assertRaisesRegex(ValueError, "clean revision"):
                    runner.source_info(root, False)
            with patch.object(runner.subprocess, "check_output", side_effect=["a" * 40, " M tracked.cpp"]), \
                    patch.object(runner, "digest", return_value="b" * 64):
                self.assertTrue(runner.source_info(root, True)["source_dirty"])

    def test_reference_pin_matches_ci(self):
        manifest = json.loads(runner.MANIFEST.read_text())
        workflow = SCRIPT.parent.parent / ".github/workflows/linux.yml"
        self.assertIn(f"LUMEN_DESIGNER_PERF_BASELINE_COMMIT: {manifest['commit']}", workflow.read_text())


if __name__ == "__main__":
    unittest.main()
