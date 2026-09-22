"""Validate operator evidence; a passing smoke is never a reader sign-off."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil

READERS = {"linux-x11": {"Orca"}, "linux-wayland": {"Orca"},
           "macos": {"VoiceOver"}, "windows": {"Narrator", "NVDA"}}
READER_CASES = {"read", "focus", "activate", "value", "editing", "dialog",
                "resize", "close_reopen"}
PLATFORM_CASES = {"ime_preedit_commit_cancel", "ime_candidate_position", "clipboard_cross_app",
                  "multiwindow_focus_dpi", "window_lifecycle", "transparent_composition",
                  "gpu_present_recovery_state", "soak_resources"}


def validate_platform(record: dict, soak: dict, platform: str) -> None:
    required = PLATFORM_CASES | ({"font_cold_start", "touchpad"} if platform == "windows" else set())
    for case in required:
        if record.get("platform_checks", {}).get(case) != "pass":
            raise ValueError(f"platform/{case} not passed")
    driver = {"linux-x11": "x11", "linux-wayland": "wayland", "macos": "cocoa", "windows": "windows"}[platform]
    for key in ("seconds", "windows", "frames", "resize_events", "stress_mib", "simulated_recoveries"):
        value = soak.get(key)
        if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value) or value < 0:
            raise ValueError(f"invalid soak metric: {key}")
    if soak.get("driver") != driver or soak.get("seconds", 0) < 3600:
        raise ValueError("soak must run at least one hour in the specified session")
    if (soak.get("windows") != 2 or soak.get("frames", 0) <= 0 or
        soak.get("resize_events", 0) <= 0 or soak.get("stress_mib", 0) < 64 or
        soak.get("simulated_recoveries") != 2 or soak.get("state_preserved") is not True):
        raise ValueError("soak did not exercise windows, resize, pressure and preserved recovery")


def validate(record: dict, commit: str, platform: str, directory: Path) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}", commit) or record.get("commit") != commit:
        raise ValueError("evidence must identify the exact 40-character source commit")
    if record.get("platform") != platform or platform not in READERS:
        raise ValueError("wrong desktop session")
    for key in ("operator", "recorded_at", "os", "desktop", "gpu_driver", "ime", "application"):
        if not isinstance(record.get(key), str) or not record[key].strip():
            raise ValueError(f"missing {key}")
    if record.get("provider") != {"linux-x11": "atspi", "linux-wayland": "atspi",
                                  "macos": "nsaccessibility", "windows": "uia"}[platform]:
        raise ValueError("wrong native provider")
    if record.get("provider_available") is not True:
        raise ValueError("native provider was not available")
    readers = record.get("readers", {})
    for name in READERS[platform]:
        reader = readers.get(name, {})
        if not reader.get("version"):
            raise ValueError(f"missing {name} version")
        for case in READER_CASES:
            if reader.get("checks", {}).get(case) != "pass":
                raise ValueError(f"{name}/{case} not passed")
    artifacts = record.get("artifacts", [])
    if not artifacts:
        raise ValueError("no trace, log or recording attached")
    for artifact in artifacts:
        relative = Path(artifact["path"])
        if relative.is_absolute() or ".." in relative.parts or relative == Path("record.json"):
            raise ValueError("artifact must use a relative, non-record path")
        path = (directory / relative).resolve()
        if not path.is_relative_to(directory.resolve()) or not path.is_file():
            raise ValueError("artifact must be a file inside the evidence directory")
        if path.stat().st_size == 0 or hashlib.sha256(path.read_bytes()).hexdigest() != artifact["sha256"]:
            raise ValueError("empty or mismatched artifact")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--record", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--platform", choices=READERS, required=True)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--soak-report", type=Path)
    args = parser.parse_args()
    try:
        record = json.loads(args.record.read_text(encoding="utf-8"))
        validate(record, args.commit, args.platform, args.record.parent)
        if args.soak_report:
            validate_platform(record, json.loads(args.soak_report.read_text(encoding="utf-8")), args.platform)
        if args.archive:
            args.archive.mkdir(parents=True, exist_ok=True)
            shutil.copy2(args.record, args.archive / "record.json")
            for item in record["artifacts"]:
                target = args.archive / item["path"]
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(args.record.parent / item["path"], target)
    except (ValueError, OSError, KeyError, TypeError, AttributeError) as error:
        parser.exit(1, f"acceptance incomplete: {error}\n")
    print(f"validated operator evidence: {args.platform} {args.commit}")


if __name__ == "__main__":
    main()
