"""Expose failure context in GitHub annotations while preserving command status."""

import argparse
from pathlib import Path
import re
import subprocess
import sys


def annotate(log: str) -> None:
    lines = log.splitlines()
    pattern = re.compile(
        r"\berror(?:\s+[A-Z]+\d+)?\s*:|\bfatal error\b|Undefined symbols|"
        r"FAILED:|FileNotFoundError:|RuntimeError:|Traceback"
    )
    indexes = [index for index, line in enumerate(lines) if pattern.search(line)]
    if not indexes:
        indexes = [max(0, len(lines) - 15)]
    end = 0
    count = 0
    for index in indexes:
        if index < end:
            continue
        start = max(0, index - 2)
        end = min(len(lines), index + 15)
        context = "\n".join(lines[start:end])[:4000]
        escaped = context.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
        print(f"::error::{escaped}")
        count += 1
        if count == 20:
            break


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.log:
        annotate(args.log.read_text(encoding="utf-8", errors="replace"))
        return 0
    command = args.command
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        parser.error("provide --log or a command after --")
    try:
        with subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace",
        ) as process:
            lines = []
            for line in process.stdout:
                print(line, end="", flush=True)
                lines.append(line)
            status = process.wait()
    except OSError as error:
        annotate(str(error))
        return 1
    if status:
        annotate("".join(lines))
    return status if status >= 0 else 128 - status


if __name__ == "__main__":
    sys.exit(main())
