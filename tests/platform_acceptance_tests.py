import copy
import hashlib
from pathlib import Path
import tempfile
import unittest
from check_platform_acceptance import validate, READER_CASES


class EvidenceTests(unittest.TestCase):
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
