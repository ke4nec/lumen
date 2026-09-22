"""Validate operator evidence; a passing smoke is never a reader sign-off."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

READERS = {"linux-x11": {"Orca"}, "linux-wayland": {"Orca"},
           "macos": {"VoiceOver"}, "windows": {"Narrator", "NVDA"}}
READER_CASES = {"read", "focus", "activate", "value", "editing", "dialog",
                "resize", "close_reopen"}


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
    args = parser.parse_args()
    try:
        record = json.loads(args.record.read_text(encoding="utf-8"))
        validate(record, args.commit, args.platform, args.record.parent)
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
