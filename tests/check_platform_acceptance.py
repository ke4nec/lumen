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
                "resize", "close_reopen", "designer_select_locate", "designer_edit_save"}
PLATFORM_CASES = {"ime_preedit_commit_cancel", "ime_candidate_position", "clipboard_cross_app",
                  "multiwindow_focus_dpi", "window_lifecycle", "transparent_composition",
                  "gpu_present_recovery_state", "soak_resources", "drag_drop_os_receive",
                  "frame_allocator_source", "designer_window_smoke", "inspector_tree_overlay",
                  "designer_edit_save_reopen", "designer_dpi_theme"}
NATIVE_ALLOCATOR_SOURCES = {"linux-x11": "glibc/malloc", "linux-wayland": "glibc/malloc",
                            "macos": "libmalloc/malloc", "windows": "ntdll/heap"}
NATIVE_ALLOCATOR_DRIVERS = {"linux-x11": "x11", "linux-wayland": "wayland",
                            "macos": "cocoa", "windows": "windows"}


def validate_frame_allocator_report(report: dict, platform: str) -> None:
    source = NATIVE_ALLOCATOR_SOURCES.get(platform)
    if source is None:
        raise ValueError(f"no supported native allocator source for {platform}")
    driver = NATIVE_ALLOCATOR_DRIVERS[platform]
    if (not isinstance(report, dict) or report.get("driver") != driver or
        report.get("frame_allocator_requested") is not True or
        report.get("frame_allocator_verified") is not True or
        report.get("frame_allocator_source") != source or report.get("state_preserved") is not True):
        raise ValueError("native allocator report has incomplete or mismatched bindings/session")
    seconds = report.get("seconds")
    if isinstance(seconds, bool) or not isinstance(seconds, (float, int)) or not math.isfinite(seconds) or seconds < 1:
        raise ValueError("invalid native allocator duration")
    for key in ("windows", "frames", "frame_allocator_frames", "frame_allocator_allocations",
                "frame_allocator_bytes", "frame_allocator_peak_bytes"):
        value = report.get(key)
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            raise ValueError(f"invalid native allocator metric: {key}")
    if (report["windows"] != 2 or report["frame_allocator_frames"] < 2 or
        report["frame_allocator_frames"] > report["frames"] or
        report["frame_allocator_peak_bytes"] > report["frame_allocator_bytes"]):
        raise ValueError("native allocator report did not verify two submitted windows or has invalid peak")


def read_frame_allocator_log(path: Path, platform: str) -> None:
    content = path.read_text(encoding="utf-8")
    if "frame_allocator_smoke pass" not in content.splitlines():
        raise ValueError("native allocator log has no successful smoke marker")
    reports = []
    for line in content.splitlines():
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict) and "frame_allocator_requested" in value:
            reports.append(value)
    if len(reports) != 1:
        raise ValueError("native allocator log requires exactly one structured report")
    validate_frame_allocator_report(reports[0], platform)


def evidence_log(record: dict, directory: Path, name: str) -> Path:
    logs = [Path(item["path"]) for item in record.get("artifacts", [])
            if Path(item["path"]).name == name]
    if len(logs) != 1:
        raise ValueError(f"acceptance requires exactly one {name} artifact")
    log = (directory / logs[0]).resolve()
    if not log.is_relative_to(directory.resolve()) or not log.is_file():
        raise ValueError(f"{name} must be inside the evidence directory")
    return log


def validate_current_log(record: dict, directory: Path, current: Path, name: str) -> None:
    artifact = evidence_log(record, directory, name)
    if hashlib.sha256(current.read_bytes()).digest() != hashlib.sha256(artifact.read_bytes()).digest():
        raise ValueError(f"{name} artifact does not match the current workflow log")


def validate_frame_allocator_artifact(record: dict, directory: Path) -> None:
    if record.get("platform_checks", {}).get("frame_allocator_source") != "pass":
        return
    log = evidence_log(record, directory, "frame-allocator-live.log")
    read_frame_allocator_log(log, record.get("platform"))


def read_designer_window_log(path: Path) -> None:
    if path.read_text(encoding="utf-8").splitlines().count("designer_window_smoke pass") != 1:
        raise ValueError("designer-live.log requires exactly one successful Designer smoke marker")


def validate_designer_window_artifact(record: dict, directory: Path) -> None:
    """Require the native Designer smoke marker when its check is passed."""
    if record.get("platform_checks", {}).get("designer_window_smoke") != "pass":
        return
    read_designer_window_log(evidence_log(record, directory, "designer-live.log"))


def validate_platform_checks(record: dict, platform: str) -> None:
    required = PLATFORM_CASES | ({"font_cold_start", "touchpad"} if platform == "windows" else set())
    for case in sorted(required):
        if record.get("platform_checks", {}).get(case) != "pass":
            raise ValueError(f"platform/{case} not passed")


def validate_platform(record: dict, soak: dict, platform: str) -> None:
    validate_platform_checks(record, platform)
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
    validate_platform_checks(record, platform)
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
    validate_designer_window_artifact(record, directory)
    validate_frame_allocator_artifact(record, directory)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--record", type=Path)
    parser.add_argument("--commit")
    parser.add_argument("--platform", choices=READERS, required=True)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--soak-report", type=Path)
    parser.add_argument("--frame-allocator-log", type=Path)
    parser.add_argument("--designer-log", type=Path)
    parser.add_argument("--validate-frame-allocator-only", action="store_true")
    args = parser.parse_args()
    try:
        if args.frame_allocator_log:
            read_frame_allocator_log(args.frame_allocator_log, args.platform)
        if args.designer_log:
            read_designer_window_log(args.designer_log)
        if args.validate_frame_allocator_only:
            if not args.frame_allocator_log:
                raise ValueError("--validate-frame-allocator-only requires --frame-allocator-log")
            print(f"validated native allocator smoke: {args.platform}")
            return
        if args.record is None or args.commit is None:
            raise ValueError("operator acceptance requires --record and --commit")
        if args.soak_report is None:
            raise ValueError("operator acceptance requires --soak-report")
        record = json.loads(args.record.read_text(encoding="utf-8"))
        validate(record, args.commit, args.platform, args.record.parent)
        if args.frame_allocator_log:
            validate_current_log(record, args.record.parent, args.frame_allocator_log,
                                 "frame-allocator-live.log")
        if args.designer_log:
            validate_current_log(record, args.record.parent, args.designer_log, "designer-live.log")
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
