import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
from check_platform_acceptance import (validate, validate_platform,
                                       validate_designer_window_artifact,
                                       READER_CASES, PLATFORM_CASES)


class EvidenceTests(unittest.TestCase):
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
