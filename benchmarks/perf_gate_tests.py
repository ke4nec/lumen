#!/usr/bin/env python3
"""Small regression-gate tests that run as part of the benchmark CTest set."""

import copy
import importlib.util
import unittest
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
        "backend": "cpu",
        "scenario": "test-1080p",
        "viewport": [1920, 1080],
        "nodes": 3,
        "commands_per_frame": 4,
        "phases": {
            "reconcile": copy.deepcopy(phase),
            "layout": copy.deepcopy(phase),
            "paint": copy.deepcopy(phase),
            "submit": copy.deepcopy(phase),
            "gpu_wait": {key: 0 for key in phase},
        },
    }


class PerfGateTests(unittest.TestCase):
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
