#!/usr/bin/env python3
"""Small regression-gate tests that run as part of the benchmark CTest set."""

import copy
import importlib.util
import unittest
import json
import tempfile
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("check_perf_regression.py")
SPEC = importlib.util.spec_from_file_location("lumen_perf_gate", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


def report():
    phase = {
        "p50_us": 100.0,
        "p95_us": 120.0,
        "allocs_p50": 10,
        "alloc_bytes_p50": 1000,
    }
    return {
        "commit": "a" * 40, "binary_sha256": "b" * 64, "frame_hash": "0123456789abcdef", "source_dirty": False,
        "toolchain": "fixture", "build_type": "Release", "platform": "linux",
        "warmup_frames": 30, "measured_frames": 300, "alpha_mode": "opaque", "clear_alpha": 255,
        "measurement_scope": "headless", "runner": {"cpu": "fixture"}, "measurement_session": "fixture",
        "scene_revision": "fixture", "dependency_revision": "fixture", "compiler_flags": {"release": "-O3"},
        "backend": "cpu",
        "scenario": "test-1080p",
        "viewport": [1920, 1080],
        "nodes": 3,
        "commands_per_frame": 4,
        "phases": {
            "frame": copy.deepcopy(phase),
            "reconcile": copy.deepcopy(phase),
            "layout": copy.deepcopy(phase),
            "paint": copy.deepcopy(phase),
            "submit": copy.deepcopy(phase),
            "gpu_wait": {key: 0 for key in phase},
        },
    }


class PerfGateTests(unittest.TestCase):
    def test_rejects_incomparable_environment_and_incomplete_phase_data(self):
        for field in GATE.IDENTITY_FIELDS:
            baseline, current = report(), report()
            current[field] = "different"
            with self.subTest(field=field):
                self.assertFalse(GATE.compare(baseline, current, .1, Path("b"), Path("c"))["passed"])
        for phase in GATE.REQUIRED_PHASES:
            current = report()
            del current["phases"][phase]
            with self.subTest(phase=phase), self.assertRaises(ValueError):
                GATE._aggregate([current])
        current = report()
        current["commit"] = "working-tree"
        with self.assertRaises(ValueError):
            GATE._aggregate([current])

    def test_cli_fails_each_metric_over_ten_percent_and_allows_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline_path = Path(directory) / "baseline.json"
            current_path = Path(directory) / "current.json"
            output = Path(directory) / "gate.json"
            baseline_path.write_text(json.dumps(report()))
            for metric in GATE.PHASE_METRICS:
                for factor, expected in ((1.1, 0), (1.11, 1)):
                    current = report()
                    # Integers avoid accidental binary float drift at the exact limit.
                    current["phases"]["submit"][metric] = round(current["phases"]["submit"][metric] * factor, 6)
                    current_path.write_text(json.dumps(current))
                    with self.subTest(metric=metric, factor=factor):
                        self.assertEqual(GATE.main(["--baseline", str(baseline_path), "--current", str(current_path),
                                                    "--report", str(output)]), expected)
            current = report()
            current["phases"]["gpu_wait"]["p95_us"] = 1
            result = GATE.compare(report(), current, .1, baseline_path, current_path)
            self.assertFalse(result["passed"])
            json.dumps(result, allow_nan=False)

    def test_repeated_runs_require_same_binary_and_deterministic_output(self):
        for field in ("commit", "binary_sha256", "frame_hash"):
            changed = report()
            changed[field] = "c" * len(changed[field])
            with self.subTest(field=field), self.assertRaises(ValueError):
                GATE._aggregate([report(), changed])

    def test_median_repeats_and_zero_gpu_wait(self):
        baseline = report()
        first = report()
        second = report()
        second["phases"]["paint"]["p50_us"] = 105.0
        third = report()
        third["phases"]["paint"]["p50_us"] = 95.0
        result = GATE.compare(
            baseline,
            GATE._aggregate([first, second, third]),
            0.10,
            Path("baseline"),
            Path("current"),
        )
        self.assertTrue(result["passed"], result["failures"])

    def test_identity_and_ten_percent_regression_fail(self):
        baseline = report()
        current = report()
        current["scenario"] = "other-1080p"
        current["phases"]["layout"]["p50_us"] = 111.0
        result = GATE.compare(
            baseline, current, 0.10, Path("baseline"), Path("current")
        )
        self.assertFalse(result["passed"])
        self.assertTrue(any(f["kind"] == "identity" for f in result["failures"]))
        self.assertTrue(any(f["kind"] == "regression" for f in result["failures"]))


if __name__ == "__main__":
    unittest.main()
