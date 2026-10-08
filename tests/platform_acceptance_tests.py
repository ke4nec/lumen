import copy
import hashlib
import json
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest
from check_platform_acceptance import (validate, validate_platform,
                                       validate_designer_window_artifact,
                                       validate_frame_allocator_artifact,
                                       validate_frame_allocator_report,
                                       read_frame_allocator_log,
                                       READER_CASES, PLATFORM_CASES)


class EvidenceTests(unittest.TestCase):
    @staticmethod
    def native_allocator_report():
        return dict(driver="wayland", seconds=3.0, windows=2, frames=100,
                    state_preserved=True, frame_allocator_requested=True,
                    frame_allocator_verified=True, frame_allocator_source="glibc/malloc",
                    frame_allocator_frames=99, frame_allocator_allocations=640,
                    frame_allocator_bytes=9000, frame_allocator_peak_bytes=4000)

    def test_native_allocator_report_requires_real_source_session_and_metrics(self):
        report = self.native_allocator_report()
        validate_frame_allocator_report(report, "linux-wayland")
        for key, value in [("driver", "dummy"), ("driver", "x11"),
                           ("frame_allocator_source", "fake-allocator"),
                           ("frame_allocator_source", "rss"),
                           ("frame_allocator_source", "command-storage"),
                           ("frame_allocator_requested", False), ("frame_allocator_verified", False),
                           ("state_preserved", False), ("seconds", float("nan")),
                           ("seconds", float("inf")), ("seconds", True), ("seconds", 0),
                           ("windows", 1), ("windows", True), ("frames", 0),
                           ("frame_allocator_frames", 1), ("frame_allocator_frames", 101),
                           ("frame_allocator_allocations", 0), ("frame_allocator_bytes", 1.5),
                           ("frame_allocator_peak_bytes", 9001), ("frame_allocator_peak_bytes", True)]:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                validate_frame_allocator_report(dict(report, **{key: value}), "linux-wayland")
        for platform in ("windows", "macos"):
            with self.subTest(platform=platform), self.assertRaises(ValueError):
                validate_frame_allocator_report(report, platform)

    def test_macos_native_allocator_requires_cocoa_and_libmalloc_together(self):
        report = dict(self.native_allocator_report(), driver="cocoa",
                      frame_allocator_source="libmalloc/malloc")
        validate_frame_allocator_report(report, "macos")
        for key, value in [("driver", "wayland"), ("driver", "dummy"),
                           ("frame_allocator_source", "glibc/malloc"),
                           ("frame_allocator_source", "command-storage")]:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                validate_frame_allocator_report(dict(report, **{key: value}), "macos")
        with self.assertRaises(ValueError):
            validate_frame_allocator_report(report, "linux-wayland")

    def test_windows_native_allocator_requires_win32_and_heap_together(self):
        report = dict(self.native_allocator_report(), driver="windows",
                      frame_allocator_source="ntdll/heap")
        validate_frame_allocator_report(report, "windows")
        for key, value in [("driver", "dummy"), ("driver", "cocoa"),
                           ("frame_allocator_source", "libmalloc/malloc"),
                           ("frame_allocator_source", "command-storage")]:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                validate_frame_allocator_report(dict(report, **{key: value}), "windows")
        with self.assertRaises(ValueError):
            validate_frame_allocator_report(report, "macos")

    def test_native_allocator_pass_requires_one_successful_structured_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / "frame-allocator-live.log"
            record = {"platform": "linux-wayland",
                      "platform_checks": {"frame_allocator_source": "pass"}, "artifacts": []}
            with self.assertRaises(ValueError):
                validate_frame_allocator_artifact(record, root)
            record["artifacts"] = [{"path": log.name, "sha256": ""}]
            with self.assertRaises(ValueError):
                validate_frame_allocator_artifact(record, root)
            report = json.dumps(self.native_allocator_report())
            for content in (report, "frame_allocator_smoke pass\n",
                            report + "\n" + report + "\nframe_allocator_smoke pass\n",
                            json.dumps(dict(self.native_allocator_report(), driver="dummy")) +
                            "\nframe_allocator_smoke pass\n"):
                log.write_text(content, encoding="utf-8")
                with self.subTest(content=content), self.assertRaises(ValueError):
                    validate_frame_allocator_artifact(record, root)
            log.write_text("libEGL warning\n" + report + "\nframe_allocator_smoke pass\n", encoding="utf-8")
            validate_frame_allocator_artifact(record, root)
            read_frame_allocator_log(log, "linux-wayland")
            record["artifacts"].append(record["artifacts"][0])
            with self.assertRaises(ValueError):
                validate_frame_allocator_artifact(record, root)
            record["platform_checks"]["frame_allocator_source"] = "pending"
            validate_frame_allocator_artifact(record, root)

    def test_operator_record_enforces_native_allocator_artifact_and_its_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            record = dict(commit="a" * 40, platform="linux-wayland", operator="fixture",
                          recorded_at="2026-10-08T00:00:00Z", os="fixture", desktop="fixture",
                          gpu_driver="fixture", ime="fixture", application="fixture",
                          provider="atspi", provider_available=True,
                          readers={"Orca": {"version": "fixture", "checks":
                                   {key: "pass" for key in READER_CASES}}},
                          platform_checks={"frame_allocator_source": "pass"},
                          artifacts=[{"path": "trace.txt", "sha256": ""}])
            (root / "trace.txt").write_text("unit fixture only", encoding="utf-8")
            record["artifacts"][0]["sha256"] = hashlib.sha256((root / "trace.txt").read_bytes()).hexdigest()
            with self.assertRaises(ValueError):
                validate(record, "a" * 40, "linux-wayland", root)
            log = root / "frame-allocator-live.log"
            log.write_text(json.dumps(self.native_allocator_report()) +
                           "\nframe_allocator_smoke pass\n", encoding="utf-8")
            record["artifacts"].append({"path": log.name,
                                       "sha256": hashlib.sha256(log.read_bytes()).hexdigest()})
            validate(record, "a" * 40, "linux-wayland", root)
            log.write_text("changed", encoding="utf-8")
            with self.assertRaises(ValueError):
                validate(record, "a" * 40, "linux-wayland", root)

    def test_native_only_cli_does_not_validate_an_operator_record(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "frame-allocator-live.log"
            log.write_text(json.dumps(self.native_allocator_report()) +
                           "\nframe_allocator_smoke pass\n", encoding="utf-8")
            command = [sys.executable, str(Path(__file__).with_name("check_platform_acceptance.py")),
                       "--platform", "linux-wayland", "--frame-allocator-log", str(log)]
            native = subprocess.run(command + ["--validate-frame-allocator-only"],
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(native.returncode, 0, native.stderr)
            self.assertIn("validated native allocator smoke", native.stdout)
            self.assertNotIn("validated operator evidence", native.stdout)
            operator = subprocess.run(command, capture_output=True, text=True, timeout=5)
            self.assertNotEqual(operator.returncode, 0)
            self.assertIn("requires --record and --commit", operator.stderr)

    def test_short_smoke_or_missing_recovery_cannot_pass_platform_acceptance(self):
        record = {"platform_checks": {key: "pass" for key in PLATFORM_CASES}}
        soak = dict(driver="x11", seconds=3600, windows=2, frames=100,
                    resize_events=20, stress_mib=64, simulated_recoveries=2, state_preserved=True)
        validate_platform(record, soak, "linux-x11")
        for key, value in [("seconds", 30), ("seconds", float("nan")), ("driver", "dummy"), ("simulated_recoveries", 0),
                           ("state_preserved", False), ("resize_events", 0)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_platform(record, dict(soak, **{key: value}), "linux-x11")

    def test_drag_drop_os_receive_is_required_platform_check(self):
        # R3 真实拖入 smoke：M15 实现批次交付 headless 契约后，OS 拖入必须进入
        # 逐平台人工清单，不能只凭 Fake host 会话测试宣称完成。
        record = {"platform_checks": {key: "pass" for key in PLATFORM_CASES}}
        soak = dict(driver="x11", seconds=3600, windows=2, frames=100,
                    resize_events=20, stress_mib=64, simulated_recoveries=2, state_preserved=True)
        validate_platform(record, soak, "linux-x11")
        for value in ("pending", None):
            with self.subTest(value=value), self.assertRaises(ValueError):
                invalid = {"platform_checks": dict(record["platform_checks"],
                                                   drag_drop_os_receive=value)}
                validate_platform(invalid, soak, "linux-x11")

    def test_frame_allocator_source_is_required_platform_check(self):
        # R6 的 fake/headless scope 不能替代三桌面生产 allocator source。
        record = {"platform_checks": {key: "pass" for key in PLATFORM_CASES}}
        soak = dict(driver="x11", seconds=3600, windows=2, frames=100,
                    resize_events=20, stress_mib=64, simulated_recoveries=2, state_preserved=True)
        validate_platform(record, soak, "linux-x11")
        for value in ("pending", None):
            with self.subTest(value=value), self.assertRaises(ValueError):
                invalid = {"platform_checks": dict(record["platform_checks"],
                                                   frame_allocator_source=value)}
                validate_platform(invalid, soak, "linux-x11")

    def test_designer_window_smoke_is_required_platform_check(self):
        # D2/D3 的真实窗口出口必须由 Designer 自身在桌面会话中通过；
        # settings/Gallery 的窗口 smoke 不能替代设计器平台证据。
        record = {"platform_checks": {key: "pass" for key in PLATFORM_CASES}}
        soak = dict(driver="x11", seconds=3600, windows=2, frames=100,
                    resize_events=20, stress_mib=64, simulated_recoveries=2, state_preserved=True)
        validate_platform(record, soak, "linux-x11")
        for value in ("pending", None):
            with self.subTest(value=value), self.assertRaises(ValueError):
                invalid = {"platform_checks": dict(record["platform_checks"],
                                                   designer_window_smoke=value)}
                validate_platform(invalid, soak, "linux-x11")

    def test_designer_window_smoke_requires_success_marker_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / "designer-live.log"
            record = {"platform_checks": {"designer_window_smoke": "pass"},
                      "artifacts": [{"path": log.name, "sha256": ""}]}
            log.write_text("libEGL warning\n", encoding="utf-8")
            record["artifacts"][0]["sha256"] = hashlib.sha256(log.read_bytes()).hexdigest()
            with self.assertRaises(ValueError):
                validate_designer_window_artifact(record, root)
            log.write_text("designer_window_smoke pass\n", encoding="utf-8")
            record["artifacts"][0]["sha256"] = hashlib.sha256(log.read_bytes()).hexdigest()
            validate_designer_window_artifact(record, root)

    def test_requires_current_commit_reader_cases_and_real_attachments(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "trace.txt").write_text("test fixture only", encoding="utf-8")
            record = dict(commit="a" * 40, platform="linux-x11", operator="fixture",
                          recorded_at="2026-09-22T00:00:00Z", os="Linux", desktop="X11",
                          gpu_driver="fixture", ime="fixture", application="settings",
                          provider="atspi", provider_available=True,
                          readers={"Orca": {"version": "fixture", "checks":
                                   {key: "pass" for key in READER_CASES}}},
                          artifacts=[{"path": "trace.txt", "sha256":
                                      hashlib.sha256((root / "trace.txt").read_bytes()).hexdigest()}])
            validate(record, "a" * 40, "linux-x11", root)
            for field, value in [("commit", "b" * 40), ("platform", "linux-wayland"),
                                 ("provider_available", False), ("artifacts", []),
                                 ("readers", {}), ("operator", "")]:
                with self.subTest(field=field), self.assertRaises(ValueError):
                    invalid = dict(record, **{field: value})
                    validate(invalid, "a" * 40, "linux-x11", root)
            invalid = copy.deepcopy(record)
            invalid["readers"]["Orca"]["checks"]["editing"] = "pending"
            with self.assertRaises(ValueError):
                validate(invalid, "a" * 40, "linux-x11", root)
            (root / "trace.txt").write_text("changed", encoding="utf-8")
            with self.assertRaises(ValueError):
                validate(record, "a" * 40, "linux-x11", root)


if __name__ == "__main__":
    unittest.main()
