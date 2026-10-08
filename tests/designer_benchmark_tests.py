"""Verify the actual F6 probe report and command-line failure paths."""

import json
import math
from pathlib import Path
import subprocess
import sys
import unittest


BINARY = Path(sys.argv[1]).resolve()
COMMON_PHASES = {"serialize", "read", "schema", "compile", "layout", "paint"}
FIXTURES = {
    "l0_12": (12, {"import"}),
    "edit_100": (100, {"edit"}),
    "outline_1000": (1000, {"outline"}),
    "virtual_list_1000": (1, set()),
}


def run(*arguments):
    return subprocess.run([str(BINARY), *arguments], text=True,
                          capture_output=True, timeout=30, check=False)


class DesignerBenchmarkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.reports = []
        for _ in range(2):
            result = run("--benchmark", "--warmup", "1", "--iterations", "3")
            if result.returncode:
                raise AssertionError(result.stderr)
            cls.reports.append(json.loads(result.stdout))

    def test_four_fixtures_export_phase_heap_and_shape_evidence(self):
        report = self.reports[0]
        self.assertEqual(report["schema"], 2)
        self.assertEqual(report["scope"], "designer_pipeline_cpp_heap")
        self.assertTrue(report["toolchain"])
        self.assertIn(report["build_type"], {"Debug", "Release", "RelWithDebInfo", "MinSizeRel", "Unspecified"})
        self.assertEqual(report["viewport"], [800, 600])
        self.assertEqual(report["warmup"], 1)
        self.assertEqual(report["iterations"], 3)
        self.assertEqual(set(report["fixtures"]), set(FIXTURES))
        for name, (nodes, extra_phases) in FIXTURES.items():
            with self.subTest(fixture=name):
                fixture = report["fixtures"][name]
                self.assertEqual(fixture["document_nodes"], nodes)
                self.assertEqual(fixture["rebuilds"], 2 if name == "edit_100" else 1)
                self.assertRegex(fixture["frame_hash"], r"^[0-9a-f]{16}$")
                self.assertNotEqual(int(fixture["frame_hash"], 16), 0)
                self.assertEqual(set(fixture["phases"]), COMMON_PHASES | extra_phases)
                for phase in fixture["phases"].values():
                    self.assertEqual(set(phase), {"p50_us", "p95_us", "allocs_p50", "alloc_bytes_p50"})
                    for value in phase.values():
                        self.assertTrue(math.isfinite(value))
                        self.assertGreaterEqual(value, 0)
                    self.assertGreaterEqual(phase["p95_us"], phase["p50_us"])
                heap = fixture["heap"]
                for metric in ("allocs_p50", "alloc_bytes_p50", "peak_bytes_p50"):
                    self.assertGreater(heap[metric], 0)
                self.assertLessEqual(heap["live_bytes_p50"], heap["peak_bytes_p50"])
                if name == "virtual_list_1000":
                    self.assertGreater(fixture["materialized_items"], 0)
                    self.assertLess(fixture["materialized_items"], 50)
                    self.assertEqual(fixture["render_nodes"], fixture["materialized_items"] + 1)
                else:
                    self.assertEqual(fixture["render_nodes"], nodes)
                    self.assertEqual(fixture["materialized_items"], 0)

    def test_repeated_processes_preserve_deterministic_output(self):
        for name in FIXTURES:
            first, second = (report["fixtures"][name] for report in self.reports)
            for field in ("document_nodes", "render_nodes", "materialized_items",
                          "rebuilds", "frame_hash"):
                with self.subTest(fixture=name, field=field):
                    self.assertEqual(first[field], second[field])

    def test_legacy_memory_probe_remains_available(self):
        result = run()
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["schema"], 1)
        self.assertEqual(report["scope"], "designer_preview_operations")
        for name in ("preview_open", "preview_edit", "virtual_list", "design_document_preview"):
            self.assertGreater(report[name]["peak_bytes"], 0)

    def test_invalid_sampling_arguments_fail_before_measurement(self):
        for arguments in (("--unknown",), ("--benchmark", "--iterations"),
                          ("--benchmark", "--iterations", "0"),
                          ("--benchmark", "--iterations", "1001"),
                          ("--benchmark", "--iterations", "1.5"),
                          ("--benchmark", "--warmup", "-1"),
                          ("--benchmark", "--warmup", "1tail")):
            with self.subTest(arguments=arguments):
                result = run(*arguments)
                self.assertEqual(result.returncode, 2)
                self.assertIn("usage error", result.stderr)
                self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
